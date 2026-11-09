#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

/* a line shell on huart2, the ST-LINK virtual com port CubeMX sets up at
   115200 8N1. printf goes there too. read_mv returns the ADC in mV. */
void shell_init(int32_t (*read_mv)(void));

/* call every loop, only blocks while a command runs */
void shell_poll(void);

/* the last grab in mV as floats, scope_len of them, for gf2's waveform
   viewer. shell_grabbed() runs after every grab: break there to look */
extern float scope_mv[];
extern uint32_t scope_len;
void shell_grabbed(void);

#endif
