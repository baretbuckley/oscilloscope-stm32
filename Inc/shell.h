#ifndef SHELL_H
#define SHELL_H

#include <stdbool.h>
#include <stddef.h>
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

/* true while a grab or a dump runs. capture_buf is the dma's ring then */
bool shell_busy(void);

/* true once after each grab */
bool shell_new_grab(void);

/* the last grab is capture_buf[0..shell_frame_len()). 0 while the shell is
   busy, and after adc, ctrl-c or a failed grab took the buffer */
size_t shell_frame_len(void);

#endif
