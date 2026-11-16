
#include <graph.h>

#include <math.h>
#include <main.h>
#include <graphics_ctrl.h>

uint16_t graphBitmap[GRAPH_HEIGHT][GRAPH_WIDTH] = {0};

struct Range {
    float min;
    float max;
};

struct Range xAxis = {0, 0};
uint16_t voltMin = 0;
uint16_t voltMax = 0;

float displayOffset = 0;
float displayDomainSize = 0;

void setTimeDomain(float min, float max) {
    xAxis = (struct Range){min, max};
}
void setVoltRange(uint16_t min, uint16_t max) {
    voltMin = min;
    voltMax = max;
}

void setDisplayDomain(float from, float to) {
    if (to < from) return;
    float cutFrom = (from < xAxis.min)? xAxis.min : from;
    float cutTo = (from > xAxis.max)? xAxis.max : to;
    displayOffset = cutFrom;
    displayDomainSize = to - cutTo;
}
void setDisplayShift(float shift) {
    displayOffset = shift;
    if (displayOffset < xAxis.min) 
        displayOffset = xAxis.min;

    if (displayOffset > xAxis.max - displayDomainSize) 
        displayOffset = xAxis.max - displayDomainSize;
}

void resetDisplay() {
    displayOffset = xAxis.min;
    displayDomainSize = xAxis.max - xAxis.min;
}

void zoomDisplay(float scale) {
    if (scale < 0) return;
    displayDomainSize *= scale;
    if (scale > 1) {
        if (displayDomainSize + displayOffset > xAxis.max) 
            displayOffset = xAxis.max - displayOffset;
        if (displayDomainSize >= xAxis.max - xAxis.min)
            resetDisplay();
    }
}
void shiftDisplay(float shift) {
    displayOffset += shift;
    if (displayOffset < xAxis.min) 
        displayOffset = xAxis.min;

    if (displayOffset > xAxis.max - displayDomainSize) 
        displayOffset = xAxis.max - displayDomainSize;
}


uint32_t mapIdx(float val, uint32_t range) {
    return roundf((val-xAxis.min)/(xAxis.max-xAxis.min) * range);
}

void buildGraph(uint16_t *data, uint32_t len) {
    printf("building graph with len %ui\n", len);
    // reset graph
    for (int x = 0; x < GRAPH_WIDTH; x++) 
        for (int y = 0; y < GRAPH_HEIGHT; y++)
            graphBitmap[y][x] = GRAPH_BKG_COLOR;

    uint32_t from = mapIdx(displayOffset, len);
    
    uint32_t to = mapIdx(displayOffset+displayDomainSize, len);

    float xScale = (float)GRAPH_WIDTH / (float)(to-from);
    float yScale = (float)GRAPH_HEIGHT / (float)(voltMax-1 - voltMin);
    printf("Building graph from %i to %i.\n", from, to);
    for (int i = from; i < to; i++) {
        uint32_t gHeight = roundf((float)(data[i]-voltMin) * yScale); 
        if (gHeight >= voltMax) continue;
        uint32_t gIdx = roundf((i - from) * xScale);

        if (gHeight >= GRAPH_HEIGHT) continue;
        if (gIdx >= GRAPH_WIDTH) {
            printf("Error: graph idx out of bounds\n");
            continue;
        }
        graphBitmap[gHeight][gIdx] = GRAPH_COLOR;
    }
}

void displayGraph() {
    Display_drawRGBBitmap(10, 10, (uint16_t*)graphBitmap, GRAPH_WIDTH, GRAPH_HEIGHT);
}