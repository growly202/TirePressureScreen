#ifndef APP_UI_H
#define APP_UI_H

#include "lvgl.h"

#define APP_WIDTH 800
#define APP_HEIGHT 480

/* Call after lv_init() and creation of the default display, on the LVGL task. */
lv_obj_t *app_ui_start(void);

#endif
