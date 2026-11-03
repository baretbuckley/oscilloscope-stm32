#ifndef SHELL_H
#define SHELL_H

#include <stdint.h>

/* USART2 on PA2/PA3, the ST-LINK virtual com port, 115200 8N1.
   printf goes there too. read_mv returns the latest ADC reading. */
void shell_init(int32_t (*read_mv)(void));

/* call every loop, only blocks while a command runs */
void shell_poll(void);

#endif
