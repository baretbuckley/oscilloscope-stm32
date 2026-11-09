#include "capture.h"

#include "main.h"

/* the 2.5 cycle sampling time plus 12.5 for 12 bits (RM0481 28.4.12). the
   prescaler is /1, both come from the .ioc, change this with them */
#define ADC_CYCLES 15U

#define TIMEOUT_MS 10U

extern ADC_HandleTypeDef hadc1;

volatile uint16_t capture_buf[CAPTURE_LEN];

static volatile int done;

/* HAL calls this from the dma interrupt when the block is in */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
  (void)hadc;
  done = 1;
}

int capture_grab(size_t n)
{
  if (n == 0 || n > CAPTURE_LEN)
    n = CAPTURE_LEN;

  done = 0;
  /* the dma moves half words (the .ioc's data width), HAL counts samples */
  if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)capture_buf, n) != HAL_OK)
    return -1;

  uint32_t start = HAL_GetTick();
  while (!done) {
    if (HAL_GetTick() - start > TIMEOUT_MS) {
      HAL_ADC_Stop_DMA(&hadc1);
      return -1;
    }
  }
  /* continuous mode keeps converting, stop it so the next grab can start */
  HAL_ADC_Stop_DMA(&hadc1);
  return 0;
}

uint32_t capture_rate_hz(void)
{
  return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADCDAC) / ADC_CYCLES;
}
