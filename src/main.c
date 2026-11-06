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
#include "tpms_espnow.h"

static const char *TAG = "tire_pressure";

#define STORAGE_NAMESPACE "tpms_ui"
#define STORAGE_KEY       "state_v1"
#define STORAGE_MAGIC     0x54505531UL /* "TPU1" */

typedef struct {
    uint32_t magic;
    uint32_t interval_ms;
    uint32_t simulation_second;
    bool has_last_sensor;
    uint32_t last_sensor_seq;
    uint32_t last_sensor_uptime_ms;
    app_ui_state_t ui;
} persisted_state_t;

static bool s_has_last_sensor = false;
static uint32_t s_last_sensor_seq = 0;
static uint32_t s_last_sensor_uptime_ms = 0;

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

static void save_persisted_state(uint32_t simulation_second)
{
    persisted_state_t state;
    memset(&state, 0, sizeof(state));
    state.magic = STORAGE_MAGIC;
    state.interval_ms = TIRE_CHART_INTERVAL_MS;
    state.simulation_second = simulation_second;
    state.has_last_sensor = s_has_last_sensor;
    state.last_sensor_seq = s_last_sensor_seq;
    state.last_sensor_uptime_ms = s_last_sensor_uptime_ms;
    app_ui_get_state(&state.ui);

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
    }
    nvs_close(handle);
}

static bool load_persisted_state(uint32_t *simulation_second, bool *sim_wheels_restored)
{
    *sim_wheels_restored = false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    persisted_state_t state;
    size_t size = sizeof(state);
    err = nvs_get_blob(handle, STORAGE_KEY, &state, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != sizeof(state) ||
        state.magic != STORAGE_MAGIC || state.interval_ms != TIRE_CHART_INTERVAL_MS) {
        return false;
    }

    *simulation_second = state.simulation_second % 60;
    s_has_last_sensor = state.has_last_sensor;
    s_last_sensor_seq = state.last_sensor_seq;
    s_last_sensor_uptime_ms = state.last_sensor_uptime_ms;
    if (s_has_last_sensor) {
        tpms_espnow_set_last_sensor(s_last_sensor_seq, s_last_sensor_uptime_ms);
    }

    app_ui_set_state(&state.ui);
    *sim_wheels_restored = state.ui.wheels[TIRE_FR].seen &&
                           state.ui.wheels[TIRE_RL].seen &&
                           state.ui.wheels[TIRE_RR].seen;

    ESP_LOGI(TAG, "Restored saved state from NVS (FL seen=%d pressure=%u.%02u bar, sim_sec=%" PRIu32 ")",
             (int)state.ui.wheels[TIRE_FL].seen,
             (unsigned)state.ui.wheels[TIRE_FL].pressure / 100,
             (unsigned)state.ui.wheels[TIRE_FL].pressure % 100,
             *simulation_second);
    return true;
}

static void update_simulation(uint32_t second)
{
    tire_sample_t sample;
    tire_simulation_sample(second, &sample);
    /* TIRE_FL (wheel 0) receives real TPMS sensor data via ESP-NOW;
     * keep random/simulated values only for the remaining 3 wheels (FR, RL, RR). */
    for (unsigned i = TIRE_FR; i < TIRE_COUNT; ++i) {
        app_ui_receive(i, sample.pressure[i]);
    }
    ESP_LOGD(TAG, "SIM t=%" PRIu32 "s (FL=ESP-NOW) FR=%u.%02u RL=%u.%02u RR=%u.%02u bar",
             second,
             (unsigned)sample.pressure[TIRE_FR] / 100, (unsigned)sample.pressure[TIRE_FR] % 100,
             (unsigned)sample.pressure[TIRE_RL] / 100, (unsigned)sample.pressure[TIRE_RL] % 100,
             (unsigned)sample.pressure[TIRE_RR] / 100, (unsigned)sample.pressure[TIRE_RR] % 100);
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
    ESP_LOGW(TAG, "HYBRID mode: FL from ESP32-C3 via ESP-NOW; FR/RL/RR simulated every 5 min");

    uint32_t simulation_second = 0;
    bool sim_wheels_restored = false;
    if (!load_persisted_state(&simulation_second, &sim_wheels_restored) || !sim_wheels_restored) {
        update_simulation(simulation_second);
        save_persisted_state(simulation_second);
    }

    lv_refr_now(display);
    ESP_ERROR_CHECK(board_backlight_on());

    esp_err_t espnow_err = tpms_espnow_init();
    if (espnow_err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed: %s", esp_err_to_name(espnow_err));
    }

    uint32_t last_report = tick_ms();
    uint32_t last_sample = last_report;
    while (true) {
        tpms_espnow_packet_t pkt;
        bool espnow_updated = false;
        while (tpms_espnow_receive(&pkt)) {
            s_has_last_sensor = true;
            s_last_sensor_seq = pkt.seq;
            s_last_sensor_uptime_ms = pkt.uptime_ms;
            espnow_updated = true;

            /* Push decoded TPMS sensor data to Front-Left wheel (TIRE_FL). */
            unsigned wheel = (pkt.wheel < TIRE_COUNT) ? pkt.wheel : TIRE_FL;
            app_ui_receive(wheel, pkt.pressure_cbar);
            ESP_LOGI(TAG,
                     "ESPNOW_DATA seq=%" PRIu32 " c3_up=%" PRIu32 "ms wheel=%u model=%s id=%s pressure=%u.%02u bar (%.1f kPa) temp=%.1f C flags=0x%02X rf_rssi=%d dBm ch=%u",
                     pkt.seq, pkt.uptime_ms, wheel, pkt.model, pkt.sensor_id,
                     (unsigned)pkt.pressure_cbar / 100, (unsigned)pkt.pressure_cbar % 100,
                     (double)pkt.pressure_kpa, (double)pkt.temperature_c,
                     (unsigned)pkt.flags, (int)pkt.rssi_dbm, (unsigned)pkt.wifi_channel);
        }
        if (espnow_updated) {
            save_persisted_state(simulation_second);
        }

        uint32_t wait_ms = lv_timer_handler();
        if (wait_ms == LV_NO_TIMER_READY || wait_ms > 20) wait_ms = 20;
        if (wait_ms < 1) wait_ms = 1;
        TickType_t wait_ticks = pdMS_TO_TICKS(wait_ms);
        vTaskDelay(wait_ticks > 0 ? wait_ticks : 1);

        uint32_t now = tick_ms();
        if ((uint32_t)(now - last_sample) >= TIRE_SAMPLE_INTERVAL_MS) {
            last_sample = now;
            simulation_second = (simulation_second + 3) % 60;
            update_simulation(simulation_second);
            save_persisted_state(simulation_second);
        }
        if ((uint32_t)(now - last_report) >= 5000) {
            ESP_LOGD(TAG, "UI task alive; LCD flushes=%" PRIu32 "; free internal heap=%u",
                     board_display_flush_count(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            last_report = now;
        }
    }
}
