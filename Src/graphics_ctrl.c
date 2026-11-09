
#include <graphics_ctrl.h>


int init_graphics(SPI_HandleTypeDef &spiHandle) {
    printf("HCLK Frequency: %u Hz\n\r", HAL_RCC_GetHCLKFreq());

    if(!DWT_CheckInitialized()) printf("DWT failed to initialize!\n\r");

    TFTShield18_create(&hi2c1);
    if (!TFTShield18_begin())
    {
        printf("Failed to initialize TFT shield!\n\r");
        return 1;
    } else {
        printf("TFT shield initialized successfully!\n\r");
    }

    TFTShield18_tftReset(true);

    DWT_delayMs(100);

    Display_create(&hspi2, DISP_CS_Pin, DISP_CS_GPIO_Port, DISP_DCX_SEL_Pin, DISP_DCX_SEL_GPIO_Port);

    Display_init(INITR_BLACKTAB);

    DWT_delayMs(100);

    Display_setRotation(1);

}

// Graph state
uint_16_t graphBitmap[GRAPH_WIDTH][GRAPH_HEIGHT] = {0};
unsigned index = 0; // Index for treating graphBitmap as circular buffer

// Scale value from graph range to graph height
uint16_t scaleValue(uint16_t val) {
    uint32_t prod = val * GRAPH_HEIGHT;
    return prod / (GRAPH_RANGE_MAX - GRAPH_RANGE_MIN);
}


void pushValue(uint_16_t val) {
    // Move to the next index, working backwords to append to front of circular buffer
    if (index == 0) index = GRAPH_WIDTH - 1;
    else index -= 1;
    
    // set current index to 
    for (int i = 0; i < GRAPH_HEIGHT; i++)
        graphBitmap[index][i] = 0;
    
    // Set pixel at height relative to value
    graphBitmap[index][scaleValue(val)] = GRAPH_COLOR;
}

void displayGraph() {
    // Split draw into 2 calls given graphBitmap is circular buffer
    Display_drawRGBBitmap(0, 0, bitMap[index], GRAPH_WIDTH-index, GRAPH_HEIGHT);

    // If index is zero, back split of buffer (0-index) is empty
    // Therefor second call to draw bitmap isn't needed
    if (index == 0) return;

    // Append at the end of the first bitmap draw
    Display_drawRGBBitmap(GRAPH_WIDTH-index, 0, bitMap, index, GRAPH_HEIGHT);
}