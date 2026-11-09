#ifndef TRIGGER_H
#define TRIGGER_H

#include <stdbool.h>
#include <stdint.h>

/* edge search over a ring of samples. positions count every sample since
   the start, the ring index is pos & mask. no hardware here, so it runs on
   the host too */

struct edge {
  uint16_t level;  /* raw counts */
  uint16_t hyst;   /* the signal has to go this far past level the other
                      way before the next edge counts, so noise near the
                      level does not fire */
  bool rise;
  bool armed;
};

void edge_init(struct edge *e, uint16_t level, uint16_t hyst, bool rise);

/* look at positions from..to-1, true with *at set on the first edge */
bool edge_find(struct edge *e, const volatile uint16_t *ring, uint32_t mask,
               uint32_t from, uint32_t to, uint32_t *at);

#endif
