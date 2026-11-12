#include "tpms_espnow.h"
#include <inttypes.h>
#include <string.h>
#include <strings.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define ESPNOW_RX_QUEUE_DEPTH     16
#define ESPNOW_LINK_TIMEOUT_MS    30000U
#define ESPNOW_CHANNEL_DWELL_MS   600U

static const char *TAG = "tpms_espnow";

static const char *const k_wheel_names[TPMS_WHEEL_COUNT] = {"FL", "FR", "RL", "RR"};
static const char *const k_fixed_sensor_ids[TPMS_WHEEL_COUNT] = {
    TPMS_SENSOR_ID_FL,
    TPMS_SENSOR_ID_FR,
    TPMS_SENSOR_ID_RL,
    TPMS_SENSOR_ID_RR,
};

static QueueHandle_t s_rx_queue = NULL;
static portMUX_TYPE s_sensor_mux = portMUX_INITIALIZER_UNLOCKED;
static tpms_wheel_sensor_state_t s_wheel_sensors[TPMS_WHEEL_COUNT];
static bool s_sensors_initialized = false;
static volatile uint32_t s_last_rx_ms = 0;
static volatile uint8_t s_locked_channel = 1;
static volatile bool s_link_established = false;
static uint32_t s_last_hb_log_ms = 0;
static uint32_t s_last_wheel_rx_ms[TPMS_WHEEL_COUNT] = {0};
static uint16_t s_last_wheel_pressure[TPMS_WHEEL_COUNT] = {0};
static bool s_has_wheel_pressure[TPMS_WHEEL_COUNT] = {false};

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void ensure_sensor_table_init_locked(void)
{
    if (s_sensors_initialized) {
        return;
    }
    memset(s_wheel_sensors, 0, sizeof(s_wheel_sensors));
    for (unsigned i = 0; i < TPMS_WHEEL_COUNT; ++i) {
        if (k_fixed_sensor_ids[i] && k_fixed_sensor_ids[i][0] != '\0') {
            strlcpy(s_wheel_sensors[i].sensor_id, k_fixed_sensor_ids[i],
                    sizeof(s_wheel_sensors[i].sensor_id));
        }
    }
    s_sensors_initialized = true;
}

static bool is_fixed_id_conflict_locked(const char *sensor_id, unsigned target_wheel)
{
    if (!sensor_id || sensor_id[0] == '\0') {
        return false;
    }
    for (unsigned i = 0; i < TPMS_WHEEL_COUNT; ++i) {
        if (i == target_wheel) {
            continue;
        }
        if (k_fixed_sensor_ids[i] && k_fixed_sensor_ids[i][0] != '\0' &&
            strcasecmp(sensor_id, k_fixed_sensor_ids[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void reset_sensor_seq_locked(void)
{
    for (unsigned i = 0; i < TPMS_WHEEL_COUNT; ++i) {
        s_wheel_sensors[i].has_last = false;
        s_wheel_sensors[i].last_seq = 0;
        s_wheel_sensors[i].last_uptime_ms = 0;
        s_has_wheel_pressure[i] = false;
        s_last_wheel_rx_ms[i] = 0;
    }
}

bool tpms_espnow_ingest_packet(const tpms_espnow_packet_t *in_pkt)
{
    if (!in_pkt || in_pkt->msg_type != TPMS_MSG_SENSOR_DATA) {
        return false;
    }

    tpms_espnow_packet_t pkt = *in_pkt;
    pkt.sensor_id[sizeof(pkt.sensor_id) - 1] = '\0';
    pkt.model[sizeof(pkt.model) - 1] = '\0';

    int matched_wheel = -1;
    bool auto_bound = false;
    bool is_duplicate = false;
    char mapped_fl[TPMS_SENSOR_ID_MAX_LEN] = {0};
    char mapped_fr[TPMS_SENSOR_ID_MAX_LEN] = {0};
    char mapped_rl[TPMS_SENSOR_ID_MAX_LEN] = {0};
    char mapped_rr[TPMS_SENSOR_ID_MAX_LEN] = {0};

    portENTER_CRITICAL_SAFE(&s_sensor_mux);
    ensure_sensor_table_init_locked();

    if (pkt.sensor_id[0] != '\0') {
        /* 1. Match by sensor_id against configured / bound wheel IDs. */
        for (unsigned i = 0; i < TPMS_WHEEL_COUNT; ++i) {
            if (s_wheel_sensors[i].sensor_id[0] != '\0' &&
                strcasecmp(pkt.sensor_id, s_wheel_sensors[i].sensor_id) == 0) {
                matched_wheel = (int)i;
                break;
            }
        }
        /* 2. If not yet matched, bind to the first unassigned wheel slot (if any). */
        if (matched_wheel < 0) {
            for (unsigned i = 0; i < TPMS_WHEEL_COUNT; ++i) {
                if (s_wheel_sensors[i].sensor_id[0] == '\0') {
                    strlcpy(s_wheel_sensors[i].sensor_id, pkt.sensor_id,
                            sizeof(s_wheel_sensors[i].sensor_id));
                    matched_wheel = (int)i;
                    auto_bound = true;
                    break;
                }
            }
        }
    } else if (pkt.wheel < TPMS_WHEEL_COUNT) {
        if (s_wheel_sensors[pkt.wheel].sensor_id[0] == '\0') {
            matched_wheel = (int)pkt.wheel;
        }
    }

    if (matched_wheel >= 0) {
        unsigned w = (unsigned)matched_wheel;
        uint32_t t_now = now_ms();
        /* If sender rebooted, clear previous sequence tracking. */
        if (s_wheel_sensors[w].has_last && pkt.uptime_ms < s_wheel_sensors[w].last_uptime_ms) {
            reset_sensor_seq_locked();
        }
        /* Ignore burst repeats (identical sequence, or identical pressure within 2.5s) */
        if (s_wheel_sensors[w].has_last &&
            ((pkt.seq != 0 && pkt.seq == s_wheel_sensors[w].last_seq) ||
             (s_has_wheel_pressure[w] &&
              pkt.pressure_cbar == s_last_wheel_pressure[w] &&
              (uint32_t)(t_now - s_last_wheel_rx_ms[w]) < 2500U))) {
            is_duplicate = true;
        } else {
            s_wheel_sensors[w].has_last = true;
            s_wheel_sensors[w].last_seq = pkt.seq;
            s_wheel_sensors[w].last_uptime_ms = pkt.uptime_ms;
            s_last_wheel_rx_ms[w] = t_now;
            s_last_wheel_pressure[w] = pkt.pressure_cbar;
            s_has_wheel_pressure[w] = true;
        }
    } else {
        strlcpy(mapped_fl, s_wheel_sensors[0].sensor_id, sizeof(mapped_fl));
        strlcpy(mapped_fr, s_wheel_sensors[1].sensor_id, sizeof(mapped_fr));
        strlcpy(mapped_rl, s_wheel_sensors[2].sensor_id, sizeof(mapped_rl));
        strlcpy(mapped_rr, s_wheel_sensors[3].sensor_id, sizeof(mapped_rr));
    }
    portEXIT_CRITICAL_SAFE(&s_sensor_mux);

    if (matched_wheel < 0) {
        ESP_LOGW(TAG,
                 "Ignored TPMS packet from unmapped sensor ID=%s (mapped FL=%s FR=%s RL=%s RR=%s)",
                 pkt.sensor_id,
                 mapped_fl[0] ? mapped_fl : "-",
                 mapped_fr[0] ? mapped_fr : "-",
                 mapped_rl[0] ? mapped_rl : "-",
                 mapped_rr[0] ? mapped_rr : "-");
        return false;
    }

    if (auto_bound) {
        ESP_LOGI(TAG, "Bound TPMS sensor ID=%s -> wheel=%d (%s)",
                 pkt.sensor_id, matched_wheel, k_wheel_names[matched_wheel]);
    }

    if (is_duplicate) {
        return false;
    }

    pkt.wheel = (uint8_t)matched_wheel;

    if (s_rx_queue) {
        if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
            /* If full, drop oldest and push newest. */
            tpms_espnow_packet_t dropped;
            (void)xQueueReceive(s_rx_queue, &dropped, 0);
            (void)xQueueSend(s_rx_queue, &pkt, 0);
        }
        return true;
    }
    return false;
}

#if TPMS_ENABLE_ESPNOW_RX
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
{
    if (!recv_info || !data || len != (int)sizeof(tpms_espnow_packet_t)) {
        return;
    }

    tpms_espnow_packet_t pkt;
    memcpy(&pkt, data, sizeof(pkt));

    if (pkt.magic != TPMS_ESPNOW_MAGIC || pkt.version != TPMS_ESPNOW_VERSION) {
        return;
    }

    /* Ensure strings are always null-terminated. */
    pkt.sensor_id[sizeof(pkt.sensor_id) - 1] = '\0';
    pkt.model[sizeof(pkt.model) - 1] = '\0';

    uint32_t t_now = now_ms();
    s_last_rx_ms = t_now;

    uint8_t rx_chan = (recv_info->rx_ctrl && recv_info->rx_ctrl->channel >= 1 && recv_info->rx_ctrl->channel <= 14)
                          ? (uint8_t)recv_info->rx_ctrl->channel
                          : pkt.wifi_channel;
    if (rx_chan >= 1 && rx_chan <= 14) {
        s_locked_channel = rx_chan;
    }

    int wifi_rssi = recv_info->rx_ctrl ? recv_info->rx_ctrl->rssi : 0;
    const uint8_t *src = recv_info->src_addr;

    if (!s_link_established) {
        s_link_established = true;
        ESP_LOGI(TAG, "ESP-NOW link established with C3 [" MACSTR "] on ch=%u (rssi=%d dBm)",
                 MAC2STR(src), (unsigned)s_locked_channel, wifi_rssi);
    }

    if (pkt.msg_type == TPMS_MSG_HEARTBEAT) {
        portENTER_CRITICAL_SAFE(&s_sensor_mux);
        ensure_sensor_table_init_locked();
        bool c3_rebooted = (pkt.seq == 0);
        if (!c3_rebooted) {
            for (unsigned i = 0; i < TPMS_WHEEL_COUNT; ++i) {
                if (s_wheel_sensors[i].has_last && pkt.uptime_ms < s_wheel_sensors[i].last_uptime_ms) {
                    c3_rebooted = true;
                    break;
                }
            }
        }
        if (c3_rebooted) {
            reset_sensor_seq_locked();
        }
        portEXIT_CRITICAL_SAFE(&s_sensor_mux);

        s_last_hb_log_ms = t_now;
        ESP_LOGI(TAG,
                 "ESPNOW_HB src=" MACSTR " ch=%u c3_up=%" PRIu32 "ms tx_seq=%" PRIu32 " %s rf_rssi=%d dBm wifi_rssi=%d dBm",
                 MAC2STR(src), (unsigned)s_locked_channel, pkt.uptime_ms, pkt.seq,
                 pkt.sensor_id[0] ? pkt.sensor_id : "raw:0",
                 (int)pkt.rssi_dbm, wifi_rssi);
        return;
    }

    if (pkt.msg_type == TPMS_MSG_SENSOR_DATA) {
        (void)tpms_espnow_ingest_packet(&pkt);
    }
}

static void tpms_espnow_sync_task(void *arg)
{
    (void)arg;

    uint8_t sweep_ch = 1;
    uint32_t boot_ms = now_ms();
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(ESPNOW_CHANNEL_DWELL_MS));

        uint32_t t_now = now_ms();
        uint32_t last_rx = s_last_rx_ms;
        uint32_t elapsed = (last_rx != 0) ? (t_now - last_rx) : (t_now - boot_ms);

        /* Stay locked on Channel 1 for the first 15s at boot, or for 30s after any packet. */
        if ((last_rx == 0 && elapsed < 15000U) ||
            (last_rx != 0 && elapsed < ESPNOW_LINK_TIMEOUT_MS)) {
            continue;
        }

        /* Step through channels 1..13 if ESP32-C3 is on an AP channel. */
        if (s_link_established) {
            s_link_established = false;
            ESP_LOGW(TAG, "ESP-NOW link idle (%u ms); sweeping channels 1..13...", (unsigned)elapsed);
        }
        sweep_ch = (uint8_t)((sweep_ch % 13U) + 1U);
        esp_wifi_set_channel(sweep_ch, WIFI_SECOND_CHAN_NONE);
    }
}
#endif /* TPMS_ENABLE_ESPNOW_RX */

esp_err_t tpms_espnow_init(void)
{
    portENTER_CRITICAL(&s_sensor_mux);
    ensure_sensor_table_init_locked();
    portEXIT_CRITICAL(&s_sensor_mux);

    if (s_rx_queue == NULL) {
        s_rx_queue = xQueueCreate(ESPNOW_RX_QUEUE_DEPTH, sizeof(tpms_espnow_packet_t));
        ESP_RETURN_ON_FALSE(s_rx_queue != NULL, ESP_ERR_NO_MEM, TAG, "RX queue alloc failed");
    }

    ESP_LOGI(TAG, "Sensor ID mapping: FL=%s FR=%s RL=%s RR=%s",
             s_wheel_sensors[0].sensor_id[0] ? s_wheel_sensors[0].sensor_id : "(auto)",
             s_wheel_sensors[1].sensor_id[0] ? s_wheel_sensors[1].sensor_id : "(auto)",
             s_wheel_sensors[2].sensor_id[0] ? s_wheel_sensors[2].sensor_id : "(auto)",
             s_wheel_sensors[3].sensor_id[0] ? s_wheel_sensors[3].sensor_id : "(auto)");

#if TPMS_ENABLE_ESPNOW_RX
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "NVS init failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "event loop init failed");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    /* Disable Wi-Fi power save so ESP-NOW broadcasts are received immediately. */
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "wifi ps none failed");

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(espnow_recv_cb), TAG, "esp_now_register_recv_cb failed");

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG, "ESP-NOW receiver initialized! S3 STA MAC: " MACSTR, MAC2STR(mac));

    xTaskCreate(tpms_espnow_sync_task, "espnow_sync", 4096, NULL, 2, NULL);
#else
    (void)s_last_rx_ms;
    (void)s_locked_channel;
    (void)s_link_established;
    (void)s_last_hb_log_ms;
    (void)now_ms;
#endif
    return ESP_OK;
}

bool tpms_espnow_receive(tpms_espnow_packet_t *out_pkt)
{
    if (!s_rx_queue || !out_pkt) {
        return false;
    }
    return xQueueReceive(s_rx_queue, out_pkt, 0) == pdTRUE;
}

void tpms_espnow_get_sensor_states(tpms_wheel_sensor_state_t out_states[TPMS_WHEEL_COUNT])
{
    if (!out_states) {
        return;
    }
    portENTER_CRITICAL(&s_sensor_mux);
    ensure_sensor_table_init_locked();
    memcpy(out_states, s_wheel_sensors, sizeof(s_wheel_sensors));
    portEXIT_CRITICAL(&s_sensor_mux);
}

void tpms_espnow_set_sensor_states(const tpms_wheel_sensor_state_t in_states[TPMS_WHEEL_COUNT])
{
    if (!in_states) {
        return;
    }
    portENTER_CRITICAL(&s_sensor_mux);
    ensure_sensor_table_init_locked();
    for (unsigned i = 0; i < TPMS_WHEEL_COUNT; ++i) {
        /* If a fixed sensor ID is configured at compile time for this wheel, always enforce it. */
        if (k_fixed_sensor_ids[i] && k_fixed_sensor_ids[i][0] != '\0') {
            strlcpy(s_wheel_sensors[i].sensor_id, k_fixed_sensor_ids[i],
                    sizeof(s_wheel_sensors[i].sensor_id));
            if (in_states[i].sensor_id[0] != '\0' &&
                strcasecmp(in_states[i].sensor_id, k_fixed_sensor_ids[i]) == 0) {
                s_wheel_sensors[i].has_last = in_states[i].has_last;
                s_wheel_sensors[i].last_seq = in_states[i].last_seq;
                s_wheel_sensors[i].last_uptime_ms = in_states[i].last_uptime_ms;
            }
        } else if (in_states[i].sensor_id[0] != '\0' &&
                   !is_fixed_id_conflict_locked(in_states[i].sensor_id, i)) {
            s_wheel_sensors[i] = in_states[i];
            s_wheel_sensors[i].sensor_id[TPMS_SENSOR_ID_MAX_LEN - 1] = '\0';
        }
    }
    portEXIT_CRITICAL(&s_sensor_mux);
}

const char *tpms_espnow_get_wheel_sensor_id(unsigned wheel)
{
    if (wheel >= TPMS_WHEEL_COUNT) {
        return "";
    }
    portENTER_CRITICAL(&s_sensor_mux);
    ensure_sensor_table_init_locked();
    const char *id = s_wheel_sensors[wheel].sensor_id;
    portEXIT_CRITICAL(&s_sensor_mux);
    return id;
}
