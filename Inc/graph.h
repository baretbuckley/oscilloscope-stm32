

#include <stdint.h>
#define GRAPH_BKG_COLOR 0x0000;
#define GRAPH_COLOR 0xf986; // R-31, G-12, B-6
#define GRAPH_WIDTH 100
#define GRAPH_HEIGHT 32





void setTimeDomain(float min, float max);
void setVoltRange(uint16_t min, uint16_t max);

// Set domain shown in graph, must be in range of data domain
void setDisplayDomain(float from, float to);
// set shift in domain, must be in range of data domain
void setDisplayShift(float shift);

void resetDisplay();

void zoomDisplay(float scale);
void shiftDisplay(float shift);




void buildGraph(uint16_t *data, uint32_t size);

void displayGraph();