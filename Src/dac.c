#include "dac.h"

#include "main.h"

#define DAC_PIN 5U

void dac_init(void)
{
  RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_DAC1EN;
  (void)RCC->AHB2ENR;

  /* analog mode, 0b11 */
  GPIOA->MODER |= 3U << (DAC_PIN * 2U);

  /* hclk is 250 MHz, anything over 160 MHz needs HFSEL = 0b10 */
  DAC1->MCR = (DAC1->MCR & ~DAC_MCR_HFSEL) | DAC_MCR_HFSEL_1;
  DAC1->CR |= DAC_CR_EN2;

  /* tWAKEUP is a few us */
  HAL_Delay(1);
}

void dac_write_mv(uint32_t mv)
{
  if (mv > VDDA_APPLI)
    mv = VDDA_APPLI;
  DAC1->DHR12R2 = mv * 4095U / VDDA_APPLI;
}
