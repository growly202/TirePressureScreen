#ifndef APP_UI_H
#define APP_UI_H

#include "lvgl.h"
#include "tire_data.h"

#define APP_WIDTH 800
#define APP_HEIGHT 480
#define APP_UI_HISTORY_POINTS 12

typedef struct {
    bool seen;
    uint16_t pressure;
} app_ui_wheel_state_t;

typedef struct {
    app_ui_wheel_state_t wheels[TIRE_COUNT];
} app_ui_state_t;

/* Call after lv_init() and creation of the default display, on the LVGL task. */
lv_obj_t *app_ui_start(void);
/* Receive a packet for one wheel, on the LVGL task. Only received wheels
 * refresh their signal timeout. Labels update immediately; history is throttled. */
void app_ui_receive(unsigned wheel, uint16_t pressure);
/* Convenience for a fresh packet containing all four wheels (simulation). */
void app_ui_update(const tire_sample_t *sample);
/* Export/import UI wheel pressures and chart history for NVS persistence. */
void app_ui_get_state(app_ui_state_t *state);
void app_ui_set_state(const app_ui_state_t *state);

#endif
