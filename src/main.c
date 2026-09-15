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
    lv_refr_now(display);
    ESP_ERROR_CHECK(board_backlight_on());

    uint32_t last_report = tick_ms();
    while (true) {
        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms == LV_NO_TIMER_READY || wait_ms > 20) wait_ms = 20;
        if (wait_ms < 1) wait_ms = 1;
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        vTaskDelay(wait_ticks > 0 ? wait_ticks : 1);

        uint32_t now = tick_ms();
        if ((uint32_t)(now - last_report) >= 5000) {
            ESP_LOGI(TAG, "UI task alive; LCD flushes=%" PRIu32 "; free internal heap=%u",
                     board_display_flush_count(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            last_report = now;
        }
    }
}
