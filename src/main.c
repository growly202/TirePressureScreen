#include <inttypes.h>
#include <string.h>
#include "app_ui.h"
#include "board_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "tpms_cc1101.h"
#include "tpms_espnow.h"

static const char *TAG = "tire_pressure";

#define STORAGE_NAMESPACE "tpms_ui"
#define STORAGE_KEY       "state_v3"
#define STORAGE_MAGIC     0x54505533UL /* "TPU3" */

typedef struct {
    uint32_t magic;
    tpms_wheel_sensor_state_t sensors[TPMS_WHEEL_COUNT];
    app_ui_state_t ui;
} persisted_state_t;

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static persisted_state_t s_last_saved_state;
static bool s_has_saved_state = false;
static uint32_t s_nvs_dirty_time = 0;
static bool s_nvs_dirty = false;

static void request_persisted_state_save(void)
{
    s_nvs_dirty = true;
    s_nvs_dirty_time = tick_ms();
}

static void process_persisted_state_save(uint32_t now)
{
    if (!s_nvs_dirty) {
        return;
    }
    if ((uint32_t)(now - s_nvs_dirty_time) < 5000U) {
        return;
    }
    s_nvs_dirty = false;

    persisted_state_t state;
    memset(&state, 0, sizeof(state));
    state.magic = STORAGE_MAGIC;
    tpms_espnow_get_sensor_states(state.sensors);
    app_ui_get_state(&state.ui);

    if (s_has_saved_state && memcmp(&s_last_saved_state, &state, sizeof(state)) == 0) {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open for write failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(handle, STORAGE_KEY, &state, sizeof(state));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS save failed: %s", esp_err_to_name(err));
    } else {
        s_last_saved_state = state;
        s_has_saved_state = true;
        ESP_LOGD(TAG, "Persisted state saved to NVS");
    }
    nvs_close(handle);
}

static bool load_persisted_state(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    persisted_state_t state;
    size_t size = sizeof(state);
    err = nvs_get_blob(handle, STORAGE_KEY, &state, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != sizeof(state) || state.magic != STORAGE_MAGIC) {
        return false;
    }

    tpms_espnow_set_sensor_states(state.sensors);
    app_ui_set_state(&state.ui);
    s_last_saved_state = state;
    s_has_saved_state = true;

    ESP_LOGI(TAG,
             "Restored saved state from NVS (IDs: FL=%s FR=%s RL=%s RR=%s)",
             tpms_espnow_get_wheel_sensor_id(TIRE_FL)[0] ? tpms_espnow_get_wheel_sensor_id(TIRE_FL) : "-",
             tpms_espnow_get_wheel_sensor_id(TIRE_FR)[0] ? tpms_espnow_get_wheel_sensor_id(TIRE_FR) : "-",
             tpms_espnow_get_wheel_sensor_id(TIRE_RL)[0] ? tpms_espnow_get_wheel_sensor_id(TIRE_RL) : "-",
             tpms_espnow_get_wheel_sensor_id(TIRE_RR)[0] ? tpms_espnow_get_wheel_sensor_id(TIRE_RR) : "-");
    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Waveshare ESP32-S3-Touch-LCD-7");
    init_nvs();
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
    ESP_LOGI(TAG, "DIRECT OOK_Receiver 4-SENSOR mode: receiving FL/FR/RL/RR via CC1101 433.92 MHz by Sensor ID");

    (void)load_persisted_state();

    lv_refr_now(display);
    ESP_ERROR_CHECK(board_backlight_on());

    esp_err_t espnow_err = tpms_espnow_init();
    if (espnow_err != ESP_OK) {
        ESP_LOGE(TAG, "TPMS pipeline init failed: %s", esp_err_to_name(espnow_err));
    }

    esp_err_t cc1101_err = tpms_cc1101_init();
    if (cc1101_err != ESP_OK) {
        ESP_LOGE(TAG, "CC1101 OOK_Receiver init failed: %s", esp_err_to_name(cc1101_err));
    }

    uint32_t last_report = tick_ms();
    while (true) {
        tpms_espnow_packet_t pkt;
        bool espnow_updated = false;
        while (tpms_espnow_receive(&pkt)) {
            espnow_updated = true;

            unsigned wheel = (pkt.wheel < TIRE_COUNT) ? pkt.wheel : TIRE_FL;
            app_ui_receive(wheel, pkt.pressure_cbar);
            ESP_LOGI(TAG,
                     "TPMS_DATA seq=%" PRIu32 " up=%" PRIu32 "ms wheel=%u model=%s id=%s pressure=%u.%02u bar (%.1f kPa) temp=%.1f C flags=0x%02X rf_rssi=%d dBm",
                     pkt.seq, pkt.uptime_ms, wheel, pkt.model, pkt.sensor_id,
                     (unsigned)pkt.pressure_cbar / 100, (unsigned)pkt.pressure_cbar % 100,
                     (double)pkt.pressure_kpa, (double)pkt.temperature_c,
                     (unsigned)pkt.flags, (int)pkt.rssi_dbm);
        }
        if (espnow_updated) {
            request_persisted_state_save();
        }

        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms == LV_NO_TIMER_READY || wait_ms > 20) wait_ms = 20;
        if (wait_ms < 1) wait_ms = 1;
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        vTaskDelay(wait_ticks > 0 ? wait_ticks : 1);

        uint32_t now = tick_ms();
        process_persisted_state_save(now);
        if ((uint32_t)(now - last_report) >= 5000) {
            ESP_LOGD(TAG, "UI task alive; LCD flushes=%" PRIu32 "; free internal heap=%u",
                     board_display_flush_count(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            last_report = now;
        }
    }
}
