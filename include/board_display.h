#ifndef BOARD_DISPLAY_H
#define BOARD_DISPLAY_H

#include "esp_err.h"
#include "lvgl.h"

/* One-shot initialization, called on the LVGL task after lv_init(). */
esp_err_t board_display_init(lv_display_t **display);
esp_err_t board_backlight_on(void);
uint32_t board_display_flush_count(void);

#endif
