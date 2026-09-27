#include <inttypes.h>
#include "app_ui.h"
#include "board_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "tire_pressure";

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void update_simulation(uint32_t second)
{
    tire_sample_t sample;
    tire_simulation_sample(second, &sample);
    app_ui_update(&sample);
    ESP_LOGI(TAG, "SIM t=%" PRIu32 "s FL=%u.%02u FR=%u.%02u RL=%u.%02u RR=%u.%02u bar",
             second,
             (unsigned)sample.pressure[TIRE_FL] / 100, (unsigned)sample.pressure[TIRE_FL] % 100,
             (unsigned)sample.pressure[TIRE_FR] / 100, (unsigned)sample.pressure[TIRE_FR] % 100,
             (unsigned)sample.pressure[TIRE_RL] / 100, (unsigned)sample.pressure[TIRE_RL] % 100,
             (unsigned)sample.pressure[TIRE_RR] / 100, (unsigned)sample.pressure[TIRE_RR] % 100);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Waveshare ESP32-S3-Touch-LCD-7");
    lv_init();
    lv_tick_set_cb(tick_ms);

    lv_display_t *display = NULL;
    ESP_ERROR_CHECK(board_display_init(&display));

    if (app_ui_start() == NULL) {
        ESP_LOGE(TAG, "Cannot create dashboard");
        lv_display_delete(display);
        return;
    }
    ESP_LOGI(TAG, "Dashboard loaded: %dx%d, LVGL %d.%d.%d", APP_WIDTH, APP_HEIGHT,
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    ESP_LOGW(TAG, "SIMULATION enabled: generated data, demo thresholds <2.00 / >2.80 bar");
    update_simulation(0);
    lv_refr_now(display);
    ESP_ERROR_CHECK(board_backlight_on());

    uint32_t last_report = tick_ms();
    uint32_t last_sample = last_report;
    uint32_t simulation_second = 0;
    while (true) {
        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms == LV_NO_TIMER_READY || wait_ms > 20) wait_ms = 20;
        if (wait_ms < 1) wait_ms = 1;
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        vTaskDelay(wait_ticks > 0 ? wait_ticks : 1);

        uint32_t now = tick_ms();
        if ((uint32_t)(now - last_sample) >= 1000) {
            uint32_t elapsed_seconds = (uint32_t)(now - last_sample) / 1000;
            last_sample += elapsed_seconds * 1000;
            simulation_second = (simulation_second + elapsed_seconds) % 60;
            update_simulation(simulation_second);
        }
        if ((uint32_t)(now - last_report) >= 5000) {
            ESP_LOGI(TAG, "UI task alive; LCD flushes=%" PRIu32 "; free internal heap=%u",
                     board_display_flush_count(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            last_report = now;
        }
    }
}
