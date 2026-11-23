#ifndef APP_H
#define APP_H

#include <stdbool.h>

/* after the MX_*_Init calls. false when the tft shield does not answer */
bool app_init(void);

/* one pass of main's loop */
void app_loop(void);

#endif
