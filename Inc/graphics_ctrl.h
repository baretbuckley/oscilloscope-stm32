#ifndef GRAPHICS_CTRL
#define GRAPHICS_CTRL
#include "main.h"
#include "graphics/Adafruit_TFTShield18_API.h"
#include "graphics/Adafruit_ST7735_API.h"


// Simple interface for handling boilerplate related to graphics

#define GRAPH_COLOR 0xf986; // R-31, G-12, B-6
#define GRAPH_WIDTH 100
#define GRAPH_HEIGHT 32
#define GRAPH_RANGE_MAX 4095
#define GRAPH_RANGE_MIN 0

int init_graphics(SPI_HandleTypeDef &spiHandle);




// Prepend new value to front of the graph
// val is expected to be in range of GRAPH_RANGE
void pushValue(uint_16_t val);


void displayGraph();



#endif