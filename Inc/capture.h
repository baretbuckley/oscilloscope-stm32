#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ADC1 converts on every TIM6 update and GPDMA1 channel 0 writes the
   results round a ring without stopping. A run watches the ring for a
   trigger edge, lets it fill past the edge, stops, and turns the ring so
   the frame starts at capture_buf[0]. CubeMX sets up all three; the
   default rate is the .ioc's 1 MHz */

#define CAPTURE_RING 8192U  /* a power of two */
#define CAPTURE_LEN 4096U   /* longest frame, the rest of the ring is room
                               for the dma to run on while the cpu looks */

/* raw 12-bit counts, 0..4095 for 0..VDDA */
extern volatile uint16_t capture_buf[CAPTURE_RING];

struct capture_trig {
  uint16_t level;  /* raw counts */
  uint16_t hyst;   /* raw counts */
  bool rise;
  uint32_t pre;    /* samples kept from before the edge */
};

/* fill capture_buf[0..n). with t NULL it takes the next n samples. with a
   trigger it waits for the edge, and if none comes within CAPTURE_AUTO_MS
   it takes what is there, like a scope's auto mode. 1 when it triggered
   (*at is the edge's index in the frame), 0 without a trigger, -1 when the
   adc or dma stopped or the cpu fell a whole ring behind */
#define CAPTURE_AUTO_MS 100U
int capture_run(const struct capture_trig *t, size_t n, size_t *at);

/* samples per second, rounded to what TIM6 can divide to, and capped at
   what the ADC can convert */
uint32_t capture_set_rate(uint32_t hz);
uint32_t capture_rate_hz(void);

#endif
