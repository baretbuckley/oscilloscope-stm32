#include "capture.h"

#include "cycles.h"
#include "main.h"
#include "trigger.h"

/* the 2.5 cycle sampling time plus 12.5 for 12 bits (RM0481 28.4.12), one
   more for the trigger to cross into the adc clock. sampling time and
   prescaler come from the .ioc, change this with them */
#define ADC_CYCLES 16U

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim6;

volatile uint16_t capture_buf[CAPTURE_RING];

/* TIM6 sits on APB1. its clock is pclk1, twice that when APB1 is divided */
static uint32_t tim_clock_hz(void)
{
  RCC_ClkInitTypeDef clk;
  uint32_t latency;

  HAL_RCC_GetClockConfig(&clk, &latency);
  uint32_t pclk = HAL_RCC_GetPCLK1Freq();
  return clk.APB1CLKDivider == RCC_HCLK_DIV1 ? pclk : 2U * pclk;
}

uint32_t capture_rate_hz(void)
{
  uint32_t psc = htim6.Instance->PSC + 1U;
  uint32_t arr = htim6.Instance->ARR + 1U;

  return (uint32_t)(tim_clock_hz() / ((uint64_t)psc * arr));
}

uint32_t capture_set_rate(uint32_t hz)
{
  uint64_t clk = tim_clock_hz();
  uint64_t adc = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADCDAC);
  uint64_t fastest = (clk * ADC_CYCLES + adc - 1U) / adc;
  uint64_t ticks = hz ? (clk + hz / 2U) / hz : fastest;

  if (ticks < fastest)
    ticks = fastest;
  if (ticks > 0x10000ULL * 0x10000ULL)
    ticks = 0x10000ULL * 0x10000ULL;

  uint32_t psc = (uint32_t)((ticks - 1U) >> 16);
  uint32_t arr = (uint32_t)(ticks / (psc + 1U)) - 1U;

  __HAL_TIM_SET_PRESCALER(&htim6, psc);
  __HAL_TIM_SET_AUTORELOAD(&htim6, arr);
  /* load the prescaler now. the adc is not started, so the TRGO this makes
     converts nothing */
  htim6.Instance->EGR = TIM_EGR_UG;
  return capture_rate_hz();
}

/* TIMING (PC5, CN10-6) is high while the cpu works on a run, in the edge
   search and the rotate, so a logic analyzer sees what struct capture_time
   counts */
static void busy(bool on)
{
  TIMING_GPIO_Port->BSRR = on ? TIMING_Pin : (uint32_t)TIMING_Pin << 16;
}

/* where the dma writes next. BNDT counts the bytes left in the block and
   is reloaded from the node when the block ends */
static uint32_t ring_pos(uint32_t ring)
{
  uint32_t left = __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle) / 2U;
  return (ring - left) & (ring - 1U);
}

static void reverse(uint16_t *b, uint32_t i, uint32_t j)
{
  while (i < j) {
    uint16_t t = b[i];
    b[i++] = b[--j];
    b[j] = t;
  }
}

/* turn the ring left so start lands at 0 */
static void rotate(uint32_t start, uint32_t ring)
{
  uint16_t *b = (uint16_t *)capture_buf;

  start &= ring - 1U;
  reverse(b, 0, start);
  reverse(b, start, ring);
  reverse(b, 0, ring);
}

static uint32_t ms_for(uint32_t samples, uint32_t rate)
{
  return (uint32_t)((uint64_t)samples * 1000U / rate) + 2U;
}

/* one run, from starting the dma to the frame being in. the polling loop,
   or with SCOPE_IRQ the dma's half and full block interrupts, move it
   along */
struct run {
  struct edge e;
  uint32_t ring, n, pre, post;
  uint32_t written;     /* samples the dma wrote, never wraps in a run */
  uint32_t scanned;     /* the edge search got this far */
  uint32_t give_up_at;  /* no edge by this many samples: auto */
  uint32_t edge_at;
  bool found, triggered;
  struct capture_time time;
};

/* search what came in since the last step. true once the frame is in */
static bool step(struct run *r)
{
  /* the edge can only come once the pre samples are in */
  if (!r->found && (int32_t)(r->written - r->scanned) > 0) {
    /* never look back more than half the ring. a frame from there still
       fits, n being at most half the ring, and a pass stays short enough
       that the dma cannot go a whole ring round unseen while it runs */
    if (r->written - r->scanned > r->ring / 2U)
      r->scanned = r->written - r->ring / 2U;
    busy(true);
    uint32_t c = cycles_now();
    r->triggered = edge_find(&r->e, capture_buf, r->ring - 1U, r->scanned,
                             r->written, &r->edge_at);
    r->time.search += cycles_now() - c;
    busy(false);
    r->time.searched +=
        (r->triggered ? r->edge_at + 1U : r->written) - r->scanned;
    r->found = r->triggered;
    r->scanned = r->written;
  }
  if (!r->found && (int32_t)(r->written - r->give_up_at) >= 0) {
    /* auto: no edge, take the newest samples */
    r->edge_at = r->written;
    r->found = true;
  }
  return r->found && r->written - r->edge_at >= r->post;
}

/* count what the dma wrote since the last look. it must not have gone a
   whole ring further, the count cannot see that */
static void catch_up(struct run *r)
{
  r->written += (ring_pos(r->ring) - r->written) & (r->ring - 1U);
}

/* the first conversion comes a full period after this */
static void start_tim6(void)
{
  __HAL_TIM_SET_COUNTER(&htim6, 0);
  HAL_TIM_Base_Start(&htim6);
}

#if SCOPE_IRQ

/* the cpu only hears from the dma every half ring, so the frame has to fit
   in the other half with room to spare: a ring of at least four frames.
   short grabs get a short ring so the interrupts come often enough */
static uint32_t ring_for(uint32_t n)
{
  uint32_t ring = 256U;

  while (ring < 4U * n && ring < CAPTURE_RING)
    ring *= 2U;
  return ring;
}

static struct run *volatile active;
static volatile bool done;

/* the dma wrote half the ring or the other half, on GPDMA1 channel 0's
   interrupt. it counts from the dma's position: when the search runs
   long, the next half can end before this returns and both come as one
   interrupt */
static void half_done(void)
{
  struct run *r = active;

  if (r == NULL)
    return;
  uint32_t c = cycles_now();
  catch_up(r);
  r->time.loops++;
  if (step(r)) {
    HAL_TIM_Base_Stop(&htim6);
    active = NULL;
    done = true;
  }
  r->time.isr += cycles_now() - c;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  (void)hadc;
  half_done();
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  (void)hadc;
  half_done();
}

/* start TIM6, then sleep until the interrupts finish the run. 0 when they
   did, -1 when the dma went quiet for limit ms */
static int wait_run(struct run *r, uint32_t limit)
{
  /* before TIM6 starts, or the first half could come and go unseen */
  done = false;
  active = r;
  start_tim6();
  uint32_t since = HAL_GetTick();

  while (!done) {
    /* the dma, the uart and the 1 ms tick wake it */
    __WFI();
    if (HAL_GetTick() - since > limit) {
      /* the dma interrupt runs above this, so it is never halfway */
      active = NULL;
      return -1;
    }
  }
  return 0;
}

#else

static uint32_t ring_for(uint32_t n)
{
  (void)n;
  return CAPTURE_RING;
}

/* start TIM6, then read the dma position and search until the frame is
   in. 0 when it is, -1 when the dma went quiet for limit ms */
static int wait_run(struct run *r, uint32_t limit)
{
  start_tim6();
  uint32_t since = HAL_GetTick();

  for (;;) {
    catch_up(r);
    r->time.loops++;
    if (step(r))
      return 0;
    if (HAL_GetTick() - since > limit)
      return -1;
  }
}

#endif

int capture_run(const struct capture_trig *t, size_t n, size_t *at,
                struct capture_time *tm)
{
  if (n == 0 || n > CAPTURE_LEN)
    n = CAPTURE_LEN;

  uint32_t rate = capture_rate_hz();
  struct run r = {
    .ring = ring_for((uint32_t)n),
    .n = (uint32_t)n,
    .pre = t ? (t->pre < n ? t->pre : (uint32_t)n - 1U) : 0,
    .found = t == NULL,
  };
  r.post = r.n - r.pre;
  r.scanned = r.pre;
  /* counted in samples, so it holds however often the cpu looks */
  r.give_up_at =
      r.pre + (uint32_t)((uint64_t)CAPTURE_AUTO_MS * rate / 1000U);
  if (t)
    edge_init(&r.e, t->level, t->hyst, t->rise);

  /* the frame is in by give_up_at + post at the latest, SCOPE_IRQ a ring
     later at most. a ring on top of that, and the dma must have stopped */
  uint32_t limit = ms_for(r.give_up_at + r.post + 2U * r.ring, rate);

  cycles_init();
  uint32_t t0 = cycles_now();

  /* HAL counts the length in samples and writes it into the node */
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)capture_buf, r.ring) != HAL_OK)
    return -1;

  int ret = wait_run(&r, limit);

  /* no more triggers, then count what came in meanwhile. one conversion
     may still be on its way, hence the one sample of slack below */
  HAL_TIM_Base_Stop(&htim6);
  catch_up(&r);
  HAL_ADC_Stop_DMA(&hadc1);
  if (ret < 0)
    return -1;

  /* check the dma did not lap the frame while the cpu looked away */
  uint32_t start = r.edge_at - r.pre;
  if (r.written - start >= r.ring)
    return -1;

  busy(true);
  uint32_t c = cycles_now();
  rotate(start, r.ring);
  r.time.rotate = cycles_now() - c;
  busy(false);
  r.time.total = cycles_now() - t0;
  if (tm)
    *tm = r.time;
  if (at)
    *at = r.pre;
  return r.triggered ? 1 : 0;
}
