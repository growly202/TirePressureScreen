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

/* Initialize Wi-Fi STA and ESP-NOW receiver with automatic channel sync. */
esp_err_t tpms_espnow_init(void);

/* Non-blocking dequeue of received TPMS sensor packet (call from LVGL task). */
bool tpms_espnow_receive(tpms_espnow_packet_t *out_pkt);

/* Seed last seen sensor sequence/uptime (e.g., restored from NVS) to ignore re-broadcasts. */
void tpms_espnow_set_last_sensor(uint32_t seq, uint32_t uptime_ms);

#endif /* TPMS_ESPNOW_H */
