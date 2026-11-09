#ifndef PROCESS_DATA
#define PROCESS_DATA


#include <stdint.h>


void processData(uint16_t *data, uint16_t length);

// Get current average and reset data
uint16_t pullData();


#endif