#ifndef TPMS_ESPNOW_H
#define TPMS_ESPNOW_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define TPMS_ESPNOW_MAGIC   0x54504D53UL /* "TPMS" in little-endian ASCII */
#define TPMS_ESPNOW_VERSION 1

typedef enum {
    TPMS_MSG_HEARTBEAT   = 0,
    TPMS_MSG_SENSOR_DATA = 1,
} tpms_espnow_msg_type_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;            /* 0..3:   TPMS_ESPNOW_MAGIC (0x54504D53) */
    uint8_t  version;          /* 4:      TPMS_ESPNOW_VERSION (1) */
    uint8_t  msg_type;         /* 5:      0 = HEARTBEAT, 1 = SENSOR_DATA */
    uint8_t  wheel;            /* 6:      0 = FL, 1 = FR, 2 = RL, 3 = RR */
    uint8_t  wifi_channel;     /* 7:      Sender current Wi-Fi channel (1..13) */
    uint16_t pressure_cbar;    /* 8..9:   Pressure in 0.01 bar (1 kPa = 0.01 bar) */
    int16_t  rssi_dbm;         /* 10..11: CC1101 433.92 MHz RSSI in dBm */
    float    pressure_kpa;     /* 12..15: Exact decoded pressure in kPa */
    float    temperature_c;    /* 16..19: Decoded temperature in Celsius */
    uint32_t seq;              /* 20..23: Monotonic TPMS sequence number */
    uint32_t uptime_ms;        /* 24..27: Sender millis() */
    uint8_t  flags;            /* 28:     Decoded sensor status/alarm flags */
    char     sensor_id[15];    /* 29..43: Sensor ID string (null-terminated) */
    char     model[20];        /* 44..63: Decoder model string (null-terminated) */
} tpms_espnow_packet_t;

_Static_assert(sizeof(tpms_espnow_packet_t) == 64, "tpms_espnow_packet_t must be 64 bytes");

/* Configured TPMS sensor IDs for the 4 wheels (case-insensitive hex string).
 * Set fixed IDs here to lock each sensor to a specific wheel position.
 * Any slot left as "" will automatically bind to the next new sensor ID received. */
#ifndef TPMS_SENSOR_ID_FL
#define TPMS_SENSOR_ID_FL "2DE995"
#endif
#ifndef TPMS_SENSOR_ID_FR
#define TPMS_SENSOR_ID_FR "7EA39A"
#endif
#ifndef TPMS_SENSOR_ID_RL
#define TPMS_SENSOR_ID_RL "D2F8BD"
#endif
#ifndef TPMS_SENSOR_ID_RR
#define TPMS_SENSOR_ID_RR "D31213"
#endif

#define TPMS_SENSOR_ID_MAX_LEN 15
#define TPMS_WHEEL_COUNT       4

typedef struct {
    bool     has_last;
    uint32_t last_seq;
    uint32_t last_uptime_ms;
    char     sensor_id[TPMS_SENSOR_ID_MAX_LEN];
} tpms_wheel_sensor_state_t;

/* Set to 1 if you also want 2.4 GHz ESP-NOW reception alongside direct CC1101.
 * Default 0 keeps Wi-Fi radio off for cleaner 3.3V power rail on CC1101 433.92 MHz. */
#ifndef TPMS_ENABLE_ESPNOW_RX
#define TPMS_ENABLE_ESPNOW_RX 0
#endif

/* Initialize TPMS sensor ID mapper and RX queue (plus ESP-NOW if enabled). */
esp_err_t tpms_espnow_init(void);

/* Submit a decoded TPMS sensor packet (from direct CC1101 or ESP-NOW) into the
 * 4-wheel Sensor ID mapper, duplicate filter, and UI RX queue. */
bool tpms_espnow_ingest_packet(const tpms_espnow_packet_t *in_pkt);

/* Non-blocking dequeue of received TPMS sensor packet (call from LVGL task). */
bool tpms_espnow_receive(tpms_espnow_packet_t *out_pkt);

/* Export/import per-wheel sensor ID mapping and last seen sequence/uptime for NVS. */
void tpms_espnow_get_sensor_states(tpms_wheel_sensor_state_t out_states[TPMS_WHEEL_COUNT]);
void tpms_espnow_set_sensor_states(const tpms_wheel_sensor_state_t in_states[TPMS_WHEEL_COUNT]);

/* Get the currently bound sensor ID string for a wheel (0=FL, 1=FR, 2=RL, 3=RR). */
const char *tpms_espnow_get_wheel_sensor_id(unsigned wheel);

#endif /* TPMS_ESPNOW_H */
