#ifndef APP_MAIN_H
#define APP_MAIN_H
#include "fc_types.h"

fc_status_t app_init(void);
void        app_tick_1khz(void);
void        app_shutdown(void);

#endif
