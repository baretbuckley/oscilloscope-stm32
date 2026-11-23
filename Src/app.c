#include "app.h"

#include <stdio.h>

#include "Adafruit_ST7735_API.h"
#include "Adafruit_TFTShield18_API.h"
#include "Adafruit_TFTShield18_Macros.h"
#include "capture.h"
#include "dwt_module.h"
#include "graph.h"
#include "main.h"
#include "process_data.h"
#include "shell.h"

// /* the average of a short grab, ADC1 belongs to the dma now */
// #define ADC_AVERAGE 64U

// extern I2C_HandleTypeDef hi2c1;
// extern SPI_HandleTypeDef hspi2;

// static bool taskBuildGraph = false;
// static bool taskDisplayGraph = false;
// static bool taskReceiveInput = true;
// static uint32_t inputPollTime;

// static int32_t adc_read_mv(void)
// {
//   uint32_t sum = 0;

//   if (capture_run(NULL, ADC_AVERAGE, NULL, NULL) < 0)
//     Error_Handler();
//   for (uint32_t i = 0; i < ADC_AVERAGE; i++)
//     sum += capture_buf[i];

//   return __HAL_ADC_CALC_DATA_TO_VOLTAGE(VDDA_APPLI, sum / ADC_AVERAGE,
//                                         ADC_RESOLUTION_12B);
// }

// bool app_init(void)
// {
//   shell_init(adc_read_mv);
//   DWT_Init();

//   printf("HCLK Frequency: %lu Hz\n\r", HAL_RCC_GetHCLKFreq());

//   if (!DWT_CheckInitialized())
//     printf("DWT failed to initialize!\n\r");

//   TFTShield18_create(&hi2c1);
//   if (!TFTShield18_begin()) {
//     printf("Failed to initialize TFT shield!\n\r");
//     return false;
//   }
//   printf("TFT shield initialized successfully!\n\r");

//   TFTShield18_tftReset(true);
//   DWT_delayMs(100);
//   Display_create(&hspi2, DISP_CS_Pin, DISP_CS_GPIO_Port, DISP_DCX_SEL_Pin,
//                  DISP_DCX_SEL_GPIO_Port);
//   Display_init(INITR_BLACKTAB);
//   DWT_delayMs(100);
//   Display_setRotation(1);
//   Display_fillScreen(ST77XX_GREEN);
//   TFTShield18_setBacklight(TFTSHIELD_BACKLIGHT_ON);

//   setVoltRange(0, 4000);
//   setTimeDomain(0.0, 1.0);
//   resetDisplay();

//   inputPollTime = HAL_GetTick();
//   DWT_delayMs(100);

//   printf("Time:\n");
//   printf("input time: %li %li\n", HAL_GetTick(), inputPollTime);
//   return true;
// }

// static void read_buttons(void)
// {
//   uint32_t input = TFTShield18_readButtons();
//   bool receivedInput = false;

//   if (!(input & TFTSHIELD_BUTTON_UP)) {
//     zoomDisplay(1.1);
//     receivedInput = true;
//   }
//   if (!(input & TFTSHIELD_BUTTON_DOWN)) {
//     zoomDisplay(0.91);
//     receivedInput = true;
//   }
//   if (!(input & TFTSHIELD_BUTTON_RIGHT)) {
//     shiftDisplay(0.01);
//     receivedInput = true;
//   }
//   if (!(input & TFTSHIELD_BUTTON_LEFT)) {
//     shiftDisplay(-0.01);
//     receivedInput = true;
//   }
//   if (receivedInput) {
//     taskBuildGraph = true;
//     inputPollTime += 100;
//   } else {
//     inputPollTime += 20;
//   }
// }

// void app_loop(void)
// {
//   shell_poll();
//   if (shell_new_grab())
//     taskBuildGraph = true;

//   /* during a grab or a dump capture_buf is the dma's ring and every pass
//      of main counts, so the display tasks wait */
//   bool idle = !shell_busy();

//   if (idle && taskBuildGraph) {
//     taskBuildGraph = false;
//     buildGraph(capture_buf, shell_frame_len());
//     taskDisplayGraph = true;
//   }
//   if (idle && taskDisplayGraph) {
//     taskDisplayGraph = false;
//     displayGraph();
//   }
//   if (HAL_GetTick() > inputPollTime)
//     taskReceiveInput = true;
//   if (idle && taskReceiveInput) {
//     taskReceiveInput = false;
//     read_buttons();
//   }

// #if SCOPE_IRQ
//   /* sleep until a byte in or out, the dma or the 1 ms tick wakes it */
//   __WFI();
// #endif
// }
