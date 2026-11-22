#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

/* a line shell on huart2, the ST-LINK virtual com port CubeMX sets up at
   115200 8N1. printf goes there too. read_mv returns the ADC in mV. */
void shell_init(int32_t (*read_mv)(void));

/* call on every pass of main. the uart sends and receives by interrupt,
   and a grab or a dump goes on over many calls, so it returns right away
   unless printf fills the 1 KB send buffer, as help does after a dump */
void shell_poll(void);

/* with SCOPE_GF2 on, the last grab in mV as floats, scope_len of them, for
   gf2's waveform viewer. shell_grabbed() runs after every grab either way:
   break there to look */
#if SCOPE_GF2
extern float scope_mv[];
extern uint32_t scope_len;
#endif
void shell_grabbed(void);

#endif
