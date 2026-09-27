#ifndef APP_UI_H
#define APP_UI_H

#include "lvgl.h"
#include "tire_data.h"

#define APP_WIDTH 800
#define APP_HEIGHT 480

/* Call after lv_init() and creation of the default display, on the LVGL task. */
lv_obj_t *app_ui_start(void);
/* Receive a packet for one wheel, on the LVGL task. Only received wheels
 * refresh their signal timeout. Labels update immediately; history is throttled. */
void app_ui_receive(unsigned wheel, uint16_t pressure);
/* Convenience for a fresh packet containing all four wheels (simulation). */
void app_ui_update(const tire_sample_t *sample);

#endif
