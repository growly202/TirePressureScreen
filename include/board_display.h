#ifndef BOARD_DISPLAY_H
#define BOARD_DISPLAY_H

#include "esp_err.h"
#include "lvgl.h"

/* One-shot initialization, called on the LVGL task after lv_init(). */
esp_err_t board_display_init(lv_display_t **display);
esp_err_t board_backlight_on(void);
esp_err_t board_set_sd_cs(bool high);
void board_i2c_lock(void);
void board_i2c_unlock(void);
uint32_t board_display_flush_count(void);

#endif
