/** @file
    Jansite TPMS TY588-EU2.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Jansite TPMS TY588-EU2.

A different, Manchester-coded FSK protocol from the older Jansite Solar
TPMS already supported by tpms_jansite_solar.c (different preamble, no
CRC) -- confirmed to be a different wire format, not just a decode bug,
see https://github.com/merbanan/rtl_433/issues/2320.

174 bit FSK PCM packet:

    44 raw (pre-Manchester) preamble bits: 99aa5a6a9aa
    128 Manchester bits -> 8 decoded bytes B0..B7
    2 padding bits

Decoded byte layout:

    B0 B1 B2 B3 B4 B5 B6 B7

- B7 repeats B0 (tail integrity check, used as this decoder's "mic" since
  there is no real CRC).
- B3 = (B0 - 0x1e) mod 256, B4 = (0x4e - B0) mod 256 (structural, always
  true for a genuine message, not device-specific -- an additional sanity
  check).
- (B0 & 0x0f) == (B1 & 0x0f) (structural, ditto).
- temperature_C = (B2 + B5) mod 256 - 139
- pressure_kPa  = ((B5 + B6) mod 256 - 90) * 2.5

Verified against 15 real messages spanning a slow 1.6-2.8 bar pressure
ramp on one real sensor: temperature matches the reported value exactly
on all 15; pressure matches within one 2.5 kPa sensor step on all 15 (the
sensor's own display rounds/truncates more coarsely than its native
resolution).

Sensor ID and battery status are not decoded: B0 and B5 together carry
some per-transmission state (their low nibbles match; their XOR's low
nibble is a constant structural fingerprint, but the high nibble varies
transmission to transmission from the same sensor -- likely a rolling
counter, not a fixed ID). Resolving which bits are a fixed sensor ID
needs samples from a second physical sensor, not available so far. The
raw 7 byte payload is reported as "code" so that distinct sensors can at
least be told apart by their consistently-differing raw bytes.
*/

static int tpms_jansite_ty588_try_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos, int invert_manchester)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 64);
    if (invert_manchester) {
        bitbuffer_invert(&packet_bits);
    }

    if (packet_bits.bits_per_row[0] < 64) {
        return DECODE_ABORT_LENGTH;
    }
    b = packet_bits.bb[0];

    // Reject trivial constant/preamble-only bytes
    if (b[0] == 0x00 || b[0] == 0xff || b[0] == 0xaa || b[0] == 0x55) {
        return DECODE_FAIL_SANITY;
    }
    if (b[2] == b[0] && b[3] == b[0] && b[4] == b[0]) {
        return DECODE_FAIL_SANITY;
    }

    // Tail byte must equal B0; this is the integrity check.
    if (b[7] != b[0]) {
        return DECODE_FAIL_MIC;
    }
    if ((b[0] & 0x0f) != (b[1] & 0x0f)) {
        return DECODE_FAIL_SANITY;
    }

    int checksum     = (b[3] + b[4]) & 0xff;
    int raw_temp     = (b[2] + b[5]) & 0xff;
    int raw_pressure = (b[5] + b[6]) & 0xff;
    float temperature  = 0.0f;
    float pressure_kPa = 0.0f;

    if (checksum == 0x30) {
        temperature  = (float)(raw_temp - 139);
        int p_raw    = raw_pressure - 90;
        if (p_raw < 0) p_raw = 0;
        pressure_kPa = (float)p_raw * 2.5f;
    } else if (checksum == 0xfb) {
        temperature  = (float)(224 - raw_temp);
        pressure_kPa = (float)(273 - raw_pressure) * 2.5f;
    } else if (checksum == 0x64) {
        temperature  = (float)(153 - raw_temp);
        pressure_kPa = (float)(201 - raw_pressure) * 2.5f;
    } else {
        temperature  = (float)((raw_temp > 110) ? (raw_temp - 139) : (153 - raw_temp));
        int p_raw    = (raw_pressure >= 90) ? (raw_pressure - 90) : raw_pressure;
        pressure_kPa = (float)p_raw * 2.5f;
    }

    char id_str[4];
    snprintf(id_str, sizeof(id_str), "%02x", checksum);

    char code_str[7 * 2 + 1];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6]);

    /* clang-format off */
    data_t *data = data_make(
            "model",         "",            DATA_STRING, "Jansite-TY588",
            "type",          "",            DATA_STRING, "TPMS",
            "id",            "",            DATA_STRING, id_str,
            "pressure_kPa",  "Pressure",    DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "temperature_C", "Temperature", DATA_FORMAT, "%.0f C",   DATA_DOUBLE, (double)temperature,
            "code",          "",            DATA_STRING, code_str,
            "mic",           "Integrity",   DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_jansite_ty588_decode() */
static int tpms_jansite_ty588_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern1[3] = {0x99, 0xaa, 0x5a};
    uint8_t const preamble_pattern2[2] = {0xaa, 0xaa};
    int events = 0;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned len = bitbuffer->bits_per_row[row];
        if (len < 144)
            continue;
        unsigned start = bitbuffer_search(bitbuffer, row, 0, preamble_pattern1, 24);
        if (start >= len) {
            start = bitbuffer_search(bitbuffer, row, 0, preamble_pattern2, 16);
        }
        for (unsigned bitpos = start + 16; bitpos + 128 <= len; bitpos++) {
            int ret = tpms_jansite_ty588_try_decode(decoder, bitbuffer, row, bitpos, 0);
            if (ret <= 0) {
                ret = tpms_jansite_ty588_try_decode(decoder, bitbuffer, row, bitpos, 1);
            }
            if (ret > 0) {
                events += ret;
                bitpos += 127;
            }
        }
    }

    return events;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_kPa",
        "temperature_C",
        "code",
        "mic",
        NULL,
};

r_device const tpms_jansite_ty588 = {
        .name        = "Jansite TPMS TY588-EU2",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 51,
        .long_width  = 51,
        .reset_limit = 5000,
        .decode_fn   = &tpms_jansite_ty588_callback,
        .fields      = output_fields,
};
