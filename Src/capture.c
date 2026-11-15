#include "capture.h"

#include "main.h"
#include "trigger.h"

/* the 2.5 cycle sampling time plus 12.5 for 12 bits (RM0481 28.4.12), one
   more for the trigger to cross into the adc clock. sampling time and
   prescaler come from the .ioc, change this with them */
#define ADC_CYCLES 16U

#define MASK (CAPTURE_RING - 1U)

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

/* where the dma writes next. BNDT counts the bytes left in the block and
   is reloaded from the node when the block ends */
static uint32_t ring_pos(void)
{
  uint32_t left = __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle) / 2U;
  return (CAPTURE_RING - left) & MASK;
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
static void rotate(uint32_t start)
{
  uint16_t *b = (uint16_t *)capture_buf;

  start &= MASK;
  reverse(b, 0, start);
  reverse(b, start, CAPTURE_RING);
  reverse(b, 0, CAPTURE_RING);
}

static uint32_t ms_for(uint32_t samples, uint32_t rate)
{
  return (uint32_t)((uint64_t)samples * 1000U / rate) + 2U;
}

int capture_run(const struct capture_trig *t, size_t n, size_t *at)
{
  if (n == 0 || n > CAPTURE_LEN)
    n = CAPTURE_LEN;

  uint32_t pre = t ? (t->pre < n ? t->pre : (uint32_t)n - 1U) : 0;
  uint32_t post = (uint32_t)n - pre;
  uint32_t rate = capture_rate_hz();
  struct edge e;

  if (t)
    edge_init(&e, t->level, t->hyst, t->rise);

  /* HAL counts the length in samples and writes it into the node */
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)capture_buf, CAPTURE_RING) != HAL_OK)
    return -1;
  __HAL_TIM_SET_COUNTER(&htim6, 0);
  HAL_TIM_Base_Start(&htim6);

  uint32_t written = 0, last = 0, scanned = pre, edge_at = 0;
  bool found = t == NULL, triggered = false;
  uint32_t since = HAL_GetTick();
  uint32_t post_wait = ms_for(post, rate);
  uint32_t wait = t ? ms_for(pre, rate) + CAPTURE_AUTO_MS : post_wait;
  int ret = 0;

  for (;;) {
    uint32_t pos = ring_pos();
    written += (pos - last) & MASK;
    last = pos;

    /* the edge can only come once the pre samples are in */
    bool was = found;
    if (!found && (int32_t)(written - scanned) > 0) {
      /* never look so far back that the frame would be overwritten */
      if (written - scanned > CAPTURE_RING - n)
        scanned = written - (CAPTURE_RING - (uint32_t)n);
      triggered = edge_find(&e, capture_buf, MASK, scanned, written, &edge_at);
      found = triggered;
      scanned = written;
    }
    if (!found && HAL_GetTick() - since > wait) {
      /* auto: no edge, take the newest samples */
      edge_at = written > pre ? written : pre;
      found = true;
    }
    if (found && !was) {
      since = HAL_GetTick();
      wait = post_wait;
    }

    if (found && (int32_t)(written - edge_at) >= (int32_t)post)
      break;
    if (HAL_GetTick() - since > wait + 10U) {
      ret = -1;
      break;
    }
  }

  /* no more triggers, then count what came in meanwhile. one conversion
     may still be on its way, hence the one sample of slack below */
  HAL_TIM_Base_Stop(&htim6);
  uint32_t pos = ring_pos();
  written += (pos - last) & MASK;
  HAL_ADC_Stop_DMA(&hadc1);
  if (ret < 0)
    return -1;

  /* check the dma did not lap the frame while the cpu looked away */
  uint32_t start = edge_at - pre;
  if (written - start >= CAPTURE_RING)
    return -1;

  rotate(start);
  if (at)
    *at = pre;
  return triggered ? 1 : 0;
}
