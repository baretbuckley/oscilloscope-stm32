#include <process_data.h>

#include <stdint.h>

// extern struct Range processedData[256];
// extern uint32_t processedDataLen = 0;

// uint16_t scaleValue(uint16_t val, uint16_t domain, uint16_t range) {
//     uint32_t prod = (uint32_t)val * range;
//     return prod / domain;
// }

// void shrinkData(uint16_t *data, uint16_t length, uint16_t targetDomain) {
//     // Reset processed data ranges
//     for (int i = 0; i < targetDomain; i++) {
//         processedData[i] = (struct Range){UINT16_MAX, 0};
//     }
//     uint16_t step = length/targetDomain;
//     uint32_t dataIdx = 0;

//     for (int i = 0; i < length; i++) {
//         uint16_t scaledIdx = scaleValue(i, length, targetDomain);
//         if (processedData[scaledIdx].min > data[i]) processedData[scaledIdx].min = data[i];
//         if (processedData[scaledIdx].max < data[i]) processedData[scaledIdx].max = data[i];
//     }
// }

// void expandData(uint16_t)

// void processData(uint16_t *data, uint16_t length, uint16_t targetDomain) {


// }

// // Get current average and reset data
// uint16_t pullData() {
//     uint32_t sum = 0;
//     uint16_t count = lapped ? 50 : averageIndex;
//     for (uint16_t i = 0; i < count; i++) {
//         sum += averagePast[i];
//     }
//     lapped = 0;
//     averageIndex = 0;
//     return count > 0 ? sum / count : 0;
// }
