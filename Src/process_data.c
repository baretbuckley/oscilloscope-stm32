#include <process_data.h>

uint16_t averagePast[50] = {0};
uint16_t averageIndex = 0;
uint16_t lapped = 0;

void processData(uint16_t *data, uint16_t length) {
    
    uint32_t sum = 0;
    for (uint16_t i = 0; i < length; i++) {
        sum += data[i];
    }
    uint16_t average = sum / length;

    averagePast[averageIndex] = average;
    if (averageIndex < 49) {
        averageIndex++;
    } else {
        averageIndex = 0;
        lapped = 1;
    }
}

// Get current average and reset data
uint16_t pullData() {
    uint32_t sum = 0;
    uint16_t count = lapped ? 50 : averageIndex;
    for (uint16_t i = 0; i < count; i++) {
        sum += averagePast[i];
    }
    lapped = 0;
    averageIndex = 0;
    return count > 0 ? sum / count : 0;
}
