#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

/* a line shell on huart2, the ST-LINK virtual com port CubeMX sets up at
   115200 8N1. printf goes there too. read_mv returns the ADC in mV. */
void shell_init(int32_t (*read_mv)(void));

/* call every loop, only blocks while a command runs */
void shell_poll(void);

#endif
