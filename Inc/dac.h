#ifndef DAC_H
#define DAC_H

#include <stdint.h>

/* DAC1 channel 2 out on PA5, which is D13 and LD2 on the board */
void dac_init(void);
void dac_write_mv(uint32_t mv);

#endif
