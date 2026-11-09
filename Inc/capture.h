#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>
#include <stdint.h>

/* ADC1 into a buffer through the GPDMA1 channel CubeMX set up. A grab
   fills the front of capture_buf at the full ADC rate and stops. */

#define CAPTURE_LEN 2048U

/* raw 12-bit counts, 0..4095 for 0..VDDA */
extern volatile uint16_t capture_buf[CAPTURE_LEN];

/* fill capture_buf[0..n), 0 when done, -1 on a dma error or timeout */
int capture_grab(size_t n);

/* samples per second */
uint32_t capture_rate_hz(void);

#endif
