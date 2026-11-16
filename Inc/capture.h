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

#define CAPTURE_LEN 4096U  /* longest frame */
/* a power of two. the rest of the ring past the frame is room for the dma
   to run on while the cpu looks. SCOPE_IRQ needs four frames of it
   (ring_for in capture.c), polling two */
#if SCOPE_IRQ
#define CAPTURE_RING (4U * CAPTURE_LEN)
#else
#define CAPTURE_RING (2U * CAPTURE_LEN)
#endif

/* raw 12-bit counts, 0..4095 for 0..VDDA */
extern volatile uint16_t capture_buf[CAPTURE_RING];

struct capture_trig {
  uint16_t level;  /* raw counts */
  uint16_t hyst;   /* raw counts */
  bool rise;
  uint32_t pre;    /* samples kept from before the edge */
};

/* where a run's time went, in HCLK cycles (cycles.h) */
struct capture_time {
  uint32_t total;     /* dma start to the turned frame, wraps past 17 s.
                         SCOPE_IRQ sleeps in between, and the counter may
                         stop while it does */
  uint32_t search;    /* inside edge_find */
  uint32_t searched;  /* samples edge_find went through */
  uint32_t loops;     /* times the cpu read the dma position, or with
                         SCOPE_IRQ the dma interrupts */
  uint32_t rotate;
  uint32_t isr;       /* SCOPE_IRQ: in the dma interrupts, search included */
};

/* fill capture_buf[0..n). with t NULL it takes the next n samples. with a
   trigger it waits for the edge, and if none comes within CAPTURE_AUTO_MS
   it takes what is there, like a scope's auto mode. SCOPE_IRQ only looks
   on half rings, so its auto can come up to half a ring later. 1 when it
   triggered (*at is the edge's index in the frame), 0 without a trigger,
   -1 when the adc or dma stopped or the cpu fell a whole ring behind. tm,
   when not NULL, gets the run's timing */
#define CAPTURE_AUTO_MS 100U
int capture_run(const struct capture_trig *t, size_t n, size_t *at,
                struct capture_time *tm);

/* samples per second, rounded to what TIM6 can divide to, and capped at
   what the ADC can convert */
uint32_t capture_set_rate(uint32_t hz);
uint32_t capture_rate_hz(void);

#endif
