#ifndef CYCLES_H
#define CYCLES_H

#include <stdint.h>

#include "stm32h5xx.h"

/* the DWT cycle counter counts HCLK cycles, 4 ns each at 250 MHz, and wraps
   after 17 s. a debugger turns it on too, cycles_init makes sure it runs
   without one. DWT_CTRL is in RM0481 66.6.1, DEMCR.TRCENA has to be set
   first for the DWT to count at all */
static inline void cycles_init(void)
{
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t cycles_now(void)
{
  return DWT->CYCCNT;
}

static inline uint32_t cycles_to_us(uint32_t cycles)
{
  return (uint32_t)((uint64_t)cycles * 1000000U / SystemCoreClock);
}

static inline uint32_t us_to_cycles(uint32_t us) {
  return (uint32_t)((uint64_t)us * SystemCoreClock / 1000000U);
}

static inline void delayUs(volatile uint32_t us) {
  uint32_t startCycle = DWT->CYCCNT;
  uint32_t delayCycles = us_to_cycles(us);
  while ((uint32_t)(DWT->CYCCNT - startCycle) < delayCycles);
}

#endif
