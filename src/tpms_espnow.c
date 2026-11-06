#include "tpms_espnow.h"
#include <inttypes.h>
#include <string.h>
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

static QueueHandle_t s_rx_queue = NULL;
static volatile uint32_t s_last_rx_ms = 0;
static volatile uint8_t s_locked_channel = 1;
static volatile bool s_link_established = false;
static volatile bool s_has_last_sensor = false;
static volatile uint32_t s_last_sensor_seq = 0;
static volatile uint32_t s_last_sensor_uptime_ms = 0;
static uint32_t s_last_hb_log_ms = 0;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

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
        if (pkt.seq == 0 || (s_has_last_sensor && pkt.uptime_ms < s_last_sensor_uptime_ms)) {
            s_has_last_sensor = false;
            s_last_sensor_seq = 0;
            s_last_sensor_uptime_ms = 0;
        }
        s_last_hb_log_ms = t_now;
        ESP_LOGI(TAG,
                 "ESPNOW_HB src=" MACSTR " ch=%u c3_up=%" PRIu32 "ms tx_seq=%" PRIu32 " %s rf_rssi=%d dBm wifi_rssi=%d dBm",
                 MAC2STR(src), (unsigned)s_locked_channel, pkt.uptime_ms, pkt.seq,
                 pkt.sensor_id[0] ? pkt.sensor_id : "raw:0",
                 (int)pkt.rssi_dbm, wifi_rssi);
        return;
    }

    if (pkt.msg_type == TPMS_MSG_SENSOR_DATA) {
        /* Only accept genuinely new CC1101 sensor decodes; ignore reliability
         * burst repeats and periodic re-broadcasts of the same sequence number. */
        if (s_has_last_sensor && pkt.seq != 0 &&
            pkt.seq == s_last_sensor_seq &&
            pkt.uptime_ms == s_last_sensor_uptime_ms) {
            return;
        }
        s_has_last_sensor = true;
        s_last_sensor_seq = pkt.seq;
        s_last_sensor_uptime_ms = pkt.uptime_ms;

        if (s_rx_queue) {
            if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
                /* If full, drop oldest and push newest. */
                tpms_espnow_packet_t dropped;
                (void)xQueueReceive(s_rx_queue, &dropped, 0);
                (void)xQueueSend(s_rx_queue, &pkt, 0);
            }
        }
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

esp_err_t tpms_espnow_init(void)
{
    if (s_rx_queue == NULL) {
        s_rx_queue = xQueueCreate(ESPNOW_RX_QUEUE_DEPTH, sizeof(tpms_espnow_packet_t));
        ESP_RETURN_ON_FALSE(s_rx_queue != NULL, ESP_ERR_NO_MEM, TAG, "RX queue alloc failed");
    }

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
    return ESP_OK;
}

bool tpms_espnow_receive(tpms_espnow_packet_t *out_pkt)
{
    if (!s_rx_queue || !out_pkt) {
        return false;
    }
    return xQueueReceive(s_rx_queue, out_pkt, 0) == pdTRUE;
}

void tpms_espnow_set_last_sensor(uint32_t seq, uint32_t uptime_ms)
{
    s_has_last_sensor = (seq != 0);
    s_last_sensor_seq = seq;
    s_last_sensor_uptime_ms = uptime_ms;
}
