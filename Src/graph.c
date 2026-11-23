
#include <graph.h>

#include <math.h>
#include <main.h>
#include "Adafruit_ST7735_API.h"
#include "grab.h"
#include <stdint.h>

uint8_t graphBitmap[GRAPH_HEIGHT][GRAPH_WIDTH/8] = {0};
// uint16_t graphBitmap[GRAPH_HEIGHT][GRAPH_WIDTH] = {0};

uint16_t *data;
uint16_t dataLen;

static uint16_t graphColor = 0xf986; // R-31, G-12, B-6

struct Range {
    float min;
    float max;
};

struct Range xAxis = {0, 0};
uint16_t voltMin = 0;
uint16_t voltMax = 0;


typedef union {
  float f;
  struct {
    unsigned int mantisa : 23;
    unsigned int exponent : 8;
    unsigned int sign : 1;
  } parts;
} float_cast;


// display offset and width are fixed point with 15 bits of decimal.
// This means it represents the range [0,2) with 1 << 15 being value of 1
uint16_t displayOffset = 0;
uint16_t displayWidth = 0;

void setGraphColor(uint16_t color) {
    graphColor = color;
}

void setTimeDomain(float min, float max) {
    xAxis = (struct Range){min, max};
}
void setVoltRange(uint16_t min, uint16_t max) {
    voltMin = min;
    voltMax = max;
}

void setDisplayDomain(float from, float to) {
    // if (to < from) return;
    // float cutFrom = (from < xAxis.min)? xAxis.min : from;
    // float cutTo = (from > xAxis.max)? xAxis.max : to;
    // displayOffset = cutFrom;
    // displayWidth = to - cutTo;
}
void setDisplayShift(float shift) {
    // displayOffset = shift;
    // if (displayOffset < xAxis.min) 
    //     displayOffset = xAxis.min;

    // if (displayOffset > xAxis.max - displayDomainSize) 
    //     displayOffset = xAxis.max - displayDomainSize;
}

void resetDisplay() {
    displayOffset = 0;
    displayWidth = 1 << 15; // 1 with 15 bits of decimal
}

int32_t zoomDisplay(float scale) {
    float_cast fscale;
    fscale.f = scale;
    if (fscale.parts.sign) return -1; // fail to zoom if negative
    if ((fscale.parts.exponent > 128)) return -1; // Fail if zooming out more than 2x

    //  mult is 17 bit value of 1 followed by 16 bits of mantisa
    uint32_t mult = fscale.parts.mantisa >> 7 | 1 << 16; 
    
    uint32_t newWidth = (uint32_t)displayWidth * mult; 
    if (fscale.parts.exponent == 128) newWidth >>= 15;
    else if ((127 - fscale.parts.exponent) < 16) newWidth >>= (16 + (127 - fscale.parts.exponent));
    else newWidth = 0;

    // If new width is >= 1 then reset display otherwise set as display width
    if (newWidth >> 15) {resetDisplay(); return displayOffset;}
    else displayWidth = newWidth;

    // Check end of display screen, if > 1 then move offset to compensate
    uint32_t end = (uint32_t)displayWidth + displayOffset;
    if (end >> 15) displayOffset = (1 << 15) >> displayWidth;

    return displayWidth;
}

int32_t shiftDisplay(float offset) {
    float_cast fshift;
    fshift.f = offset;

    if ((fshift.parts.exponent > 127)) return -1;
    if ((127-fshift.parts.exponent) > 15) return displayOffset;
    uint16_t shift = fshift.parts.mantisa >> (127 - fshift.parts.exponent);
    
    if (fshift.parts.sign) {// negative 
        if (shift > displayOffset) displayOffset = 0;
        else displayOffset -= shift;
    } else { // positive
        if (shift > ((1<<15) - displayOffset - displayWidth))
            displayOffset = (1<<15) - displayWidth; 
        else
            displayOffset += shift;
    }
    return displayOffset;
}


uint32_t mapIdx(float val, uint16_t range) {
    return roundf((val-xAxis.min)/(xAxis.max-xAxis.min) * range);
}

void buildGraph(uint16_t *data, uint32_t len) {
    // reset graph
    for (int x = 0; x < GRAPH_WIDTH/8; x++) 
        for (int y = 0; y < GRAPH_HEIGHT; y++)
            graphBitmap[y][x] = 0;

    uint32_t from = ((uint32_t)(displayOffset) * len) >> 15;
    
    uint32_t to = ((uint32_t)(displayOffset+displayWidth) * len) >> 15;

    for (int i = from; i < to; i++) {
        uint32_t gHeight = (uint32_t)(data[i] - voltMin) * GRAPH_HEIGHT / voltMax; 
        uint32_t gIdx = (uint32_t)(i - from) * GRAPH_WIDTH / (to - from);

        if (gHeight >= GRAPH_HEIGHT) 
            gHeight = GRAPH_HEIGHT-1;
        if (gIdx >= GRAPH_WIDTH) continue;
        graphBitmap[gHeight][gIdx/8] |= 1 << (gIdx%8);
    }
}

void displayGraph() {
    Display_drawBitmapBg(10, 10, (uint8_t*)graphBitmap, GRAPH_WIDTH, GRAPH_HEIGHT, graphColor, GRAPH_BKG_COLOR);
    // Display_drawRGBBitmap(10, 10, (uint16_t*)graphBitmap, GRAPH_WIDTH, GRAPH_HEIGHT);
}