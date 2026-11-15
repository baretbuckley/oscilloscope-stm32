#include "trigger.h"

void edge_init(struct edge *e, uint16_t level, uint16_t hyst, bool rise)
{
  e->level = level;
  e->hyst = hyst;
  e->rise = rise;
  e->armed = false;
}

bool edge_find(struct edge *e, const volatile uint16_t *ring, uint32_t mask,
               uint32_t from, uint32_t to, uint32_t *at)
{
  uint32_t lo = e->level > e->hyst ? e->level - e->hyst : 0;
  uint32_t hi = (uint32_t)e->level + e->hyst;

  for (uint32_t pos = from; pos != to; pos++) {
    uint32_t v = ring[pos & mask];

    if (e->rise ? v <= lo : v >= hi) {
      e->armed = true;
    } else if (e->armed && (e->rise ? v >= e->level : v <= e->level)) {
      e->armed = false;
      *at = pos;
      return true;
    }
  }
  return false;
}
