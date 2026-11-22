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
    if (r->written - r->scanned > r->ring / 2U) {
      r->scanned = r->written - r->ring / 2U;
      /* armed was seen before the gap, not just before scanned */
      r->e.armed = false;
    }
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

/* the run going on, and what capture_poll needs to end it */
static struct run cur;
static bool running;
static uint32_t since, limit, t0;

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

/* checked from main. the dma interrupt sets done, so with it set the run
   is over */
static bool finished(void)
{
  return done;
}

/* before TIM6 starts, or the first half could come and go unseen */
static void start_run(struct run *r)
{
  done = false;
  active = r;
  start_tim6();
}

/* the dma went quiet. it runs above main, so it is never halfway */
static void drop_run(void)
{
  active = NULL;
}

#else

static uint32_t ring_for(uint32_t n)
{
  (void)n;
  return CAPTURE_RING;
}

/* catch_up cannot tell a look a whole ring late from one on time, the
   position is the same. the dma's half and full interrupts can: they come
   every half ring whatever main does. this counts them */
static volatile uint32_t halves;
static bool lost;

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
  (void)hadc;
  halves++;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  (void)hadc;
  halves++;
}

/* one look at the dma position and a search of what is new. true once the
   frame is in, or once a lap went by unseen (lost) */
static bool finished(void)
{
  /* halves first: one that ends after this read is in written anyway */
  uint32_t h = halves;

  catch_up(&cur);
  cur.time.loops++;
  /* written misses a whole ring, two halves, when a lap went unseen. one
     half of slack for the interrupt still waiting to run */
  if (h > cur.written / (cur.ring / 2U) + 1U) {
    lost = true;
    return true;
  }
  return step(&cur);
}

static void start_run(struct run *r)
{
  (void)r;
  halves = 0;
  lost = false;
  start_tim6();
}

static void drop_run(void)
{
}

#endif

int capture_start(const struct capture_trig *t, size_t n)
{
  if (running)
    return -1;
  if (n == 0 || n > CAPTURE_LEN)
    n = CAPTURE_LEN;

  uint32_t rate = capture_rate_hz();
  cur = (struct run){
    .ring = ring_for((uint32_t)n),
    .n = (uint32_t)n,
    .pre = t ? (t->pre < n ? t->pre : (uint32_t)n - 1U) : 0,
    .found = t == NULL,
  };
  cur.post = cur.n - cur.pre;
  cur.scanned = cur.pre;
  /* counted in samples, so it holds however often the cpu looks */
  cur.give_up_at =
      cur.pre + (uint32_t)((uint64_t)CAPTURE_AUTO_MS * rate / 1000U);
  if (t)
    edge_init(&cur.e, t->level, t->hyst, t->rise);

  /* the frame is in by give_up_at + post at the latest, SCOPE_IRQ a ring
     later at most. a ring on top of that, and the dma must have stopped */
  limit = ms_for(cur.give_up_at + cur.post + 2U * cur.ring, rate);

  cycles_init();
  t0 = cycles_now();

  /* HAL counts the length in samples and writes it into the node */
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)capture_buf, cur.ring) != HAL_OK)
    return -1;
  running = true;
  start_run(&cur);
  since = HAL_GetTick();
  return 0;
}

int capture_poll(size_t *at, struct capture_time *tm)
{
  if (!running)
    return -1;
  int ret = 0;
  if (!finished()) {
    if (HAL_GetTick() - since <= limit)
      return CAPTURE_RUNNING;
    drop_run();
    ret = -1;
  }
#if !SCOPE_IRQ
  if (lost)
    ret = -1;
#endif
  running = false;

  /* no more triggers, then count what came in meanwhile. one conversion
     may still be on its way, hence the one sample of slack below */
  HAL_TIM_Base_Stop(&htim6);
  catch_up(&cur);
  HAL_ADC_Stop_DMA(&hadc1);
  if (ret < 0)
    return -1;

  /* check the dma did not lap the frame while the cpu looked away */
  uint32_t start = cur.edge_at - cur.pre;
  if (cur.written - start >= cur.ring)
    return -1;

  busy(true);
  uint32_t c = cycles_now();
  rotate(start, cur.ring);
  cur.time.rotate = cycles_now() - c;
  busy(false);
  cur.time.total = cycles_now() - t0;
  if (tm)
    *tm = cur.time;
  if (at)
    *at = cur.pre;
  return cur.triggered ? 1 : 0;
}

void capture_abort(void)
{
  if (!running)
    return;
  drop_run();
  running = false;
  HAL_TIM_Base_Stop(&htim6);
  HAL_ADC_Stop_DMA(&hadc1);
}

int capture_run(const struct capture_trig *t, size_t n, size_t *at,
                struct capture_time *tm)
{
  if (capture_start(t, n) < 0)
    return -1;

  int ret;
  while ((ret = capture_poll(at, tm)) == CAPTURE_RUNNING) {
#if SCOPE_IRQ
    /* the dma and the 1 ms tick wake it */
    __WFI();
#endif
  }
  return ret;
}
