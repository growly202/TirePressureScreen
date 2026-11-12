/** @file
    Jansite TPMS TY-468-eu2 / KKMOON TPMS.

    Copyright (C) 2026 Benjamin Larsson

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "decoder.h"

/**
Jansite TPMS TY-468-eu2 / KKMOON TPMS.

https://github.com/merbanan/rtl_433/issues/2025

Same SP372-chip-family encoding as tpms_imars_t240.c (see
https://github.com/merbanan/rtl_433/issues/1820, confirmed the same by
\@zuckschwerdt in #2025) -- OOK, Manchester encoded, ~50 us half-bit.

Packet structure: 32-bit raw alternating preamble (0xaaaaaaaa), then
128 raw bits Manchester decoding to 8 bytes (B0..B7).

Decoded byte layout:

    B0 B1 B2 B3 B4 B5 B6 B7

- B7 repeats B0 (tail integrity check)
- (B0 & 0x0f) == (B1 & 0x0f)
- B3 and B4 are each an affine function of B0 (B3 = B0 - k1, B4 = k2 - B0),
  so (B3 + B4) mod 256 == (k2 - k1) mod 256 is a fixed per-unit checksum

Two physical sensor units are confirmed in the issue's captures, each
with its own checksum and calibration constants, both cross-checked
against real ground-truth PSI/C readings posted in the issue (comments
#5 and #32):

    temperature_C = temp_offset - ((B2 + B5) mod 256)
    pressure_kPa  = (pressure_offset - ((B5 + B6) mod 256)) * 2.5

    checksum  temp_offset  pressure_offset
    0xfb      224          273
    0x64      153          201

The 2.5 kPa/unit pressure scale matches tpms_jansite_solar's v2/v3 and
tpms_imars_t240's own (unconfirmed) guess -- likely a hardware-fixed ADC
scale shared across this whole chip family, with checksum/temperature/
pressure offsets all individually calibrated per physical sensor unit.

Only these two units are known; a third unit would need its own
checksum and offsets derived and added here.
*/

static int tpms_jansite_ty468_try_extract(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    if (bitpos < 6 || bitpos + 64 > bitbuffer->bits_per_row[row]) {
        return 0;
    }

    uint8_t pre = 0;
    bitbuffer_extract_bytes(bitbuffer, row, bitpos - 6, &pre, 6);

    uint8_t b[8] = {0};
    bitbuffer_extract_bytes(bitbuffer, row, bitpos, b, 64);

    // Convert from IEEE Manchester (01->0, 10->1) to Thomas Manchester (01->1, 10->0)
    for (int i = 0; i < 8; ++i) {
        b[i] ^= 0xff;
    }

    uint8_t K = b[7];

    // Reject trivial constant/preamble-only bytes
    if (K == 0x00 || K == 0xff || K == 0xaa || K == 0x55) {
        return 0;
    }
    if (b[2] == K && b[3] == K && b[4] == K) {
        return 0;
    }
    if ((b[1] & 0x0f) != (K & 0x0f)) {
        return 0;
    }

    if ((pre & 0xfc) == 0x00) {
        // Normal phase preamble: B0 must equal B7 (K)
        if (b[0] != K) {
            return 0;
        }
    } else {
        // Pre-trigger shifted preamble: preamble bits before the first 2T gap were 1s (Thomas 0s),
        // and the 2T gap emitted a 0 (Thomas 1) right before the first raw 1 (Thomas 0) bit of B0.
        uint8_t raw_k = K ^ 0xff;
        uint8_t raw_b0 = b[0] ^ 0xff;
        uint16_t pre_b0 = ((uint16_t)(pre & 0xfc) << 6) | raw_b0; // 14 bits: 6 pre + 8 raw_b0
        // Find highest 1-bit in raw_k (bits 7..0)
        int msb = 7;
        while (msb >= 0 && ((raw_k >> msb) & 1) == 0) {
            --msb;
        }
        if (msb < 0) {
            return 0;
        }
        uint8_t data_mask = (uint8_t)((1U << (msb + 1)) - 1U);
        if ((raw_b0 & data_mask) != (raw_k & data_mask)) {
            return 0;
        }
        // Bit at position (msb + 1) in pre_b0 must be 0 (from the 2T gap), and the 5 bits above it must be 11111
        uint16_t sync_window = (pre_b0 >> (msb + 1)) & 0x3f;
        if (sync_window != 0x3e) {
            return 0;
        }
        b[0] = K;
    }

    // Unscramble payload bytes using per-packet rolling key K = B0 = B7:
    //   d[1] (flags)        = (B1 - K) & 0xff
    //   d[2] (temp + 50)    = (B2 + K) & 0xff
    //   d[3..5] (24-bit ID) = (B3 - K), (B4 + K), (B5 - K)
    //   d[6] (press / 2.5)  = (B6 + K) & 0xff
    int flags          = (b[1] - K) & 0xff;
    int temp_raw       = (b[2] + K) & 0xff;
    int temperature_C  = temp_raw - 50;
    uint8_t id0        = (b[3] - K) & 0xff;
    uint8_t id1        = (b[4] + K) & 0xff;
    uint8_t id2        = (b[5] - K) & 0xff;
    int press_raw      = (b[6] + K) & 0xff;
    float pressure_kPa = (float)press_raw * 2.5f;

    char id_str[7];
    snprintf(id_str, sizeof(id_str), "%02X%02X%02X", id0, id1, id2);

    char flags_str[3];
    snprintf(flags_str, sizeof(flags_str), "%02X", flags);

    char code_str[8 * 2 + 1];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Jansite-TY468",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "pressure_kpa",     "Pressure",     DATA_FORMAT, "%.1f kPa", DATA_DOUBLE, (double)pressure_kPa,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C",   DATA_DOUBLE, (double)temperature_C,
            "flags",            "Flags",        DATA_STRING, flags_str,
            "code",             "",             DATA_STRING, code_str,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

static int tpms_jansite_ty468_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned len = bitbuffer->bits_per_row[row];
        if (len < 70) {
            continue;
        }
        // Scan from the end of the row backwards so trailing edge pulses or merged subframes never shift bitpos
        for (int offset = 64; (int)len - offset >= 6; ++offset) {
            if (tpms_jansite_ty468_try_extract(decoder, bitbuffer, row, len - (unsigned)offset) > 0) {
                return 1;
            }
        }
    }

    return 0;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "pressure_kPa",
        "pressure_kpa",
        "temperature_C",
        "flags",
        "code",
        "mic",
        NULL,
};

r_device const tpms_jansite_ty468 = {
        .name        = "Jansite TPMS TY-468-eu2 / KKMOON TPMS",
        .modulation  = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_width = 110,
        .long_width  = 110,
        .gap_limit   = 1000,
        .reset_limit = 35000,
        .tolerance   = 65,
        .decode_fn   = &tpms_jansite_ty468_decode,
        .fields      = output_fields,
};
