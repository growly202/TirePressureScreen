/** @file
    Jansite FSK 7 byte Manchester encoded checksummed TPMS data.

    Copyright (C) 2019 Andreas Spiess and Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Jansite Solar TPMS (Internal/External) Model TY02S.
- Working Temperature:-40 °C to 125 °C
- Working Frequency: 433.92MHz+-38KHz
- Tire monitoring range value: 0kPa-350kPa+-7kPa

Data layout (nibbles):

    II II II IS PP TT CC

- I: 28 bit ID
- S: 4 bit Status (deflation alarm, battery low etc)
- P: 8 bit Pressure (best guess quarter PSI, i.e. ~0.58 kPa)
- T: 8 bit Temperature (deg. C offset by 50)
- C: 8 bit Checksum
- The preamble is 0xaa..aa9 (or 0x55..556 depending on polarity)
*/

#include "decoder.h"

static int tpms_jansite_decode(r_device *decoder, bitbuffer_t *bitbuffer, unsigned row, unsigned bitpos)
{
    bitbuffer_t packet_bits = {0};
    uint8_t *b;
    unsigned id;
    int flags;
    int pressure;
    int temperature;

    bitbuffer_manchester_decode(bitbuffer, row, bitpos, &packet_bits, 56);

    if (packet_bits.bits_per_row[0] < 56) {
        return DECODE_FAIL_SANITY;
    }
    b = packet_bits.bb[0];

    // Reject trivial constant or alternating preamble noise
    if ((b[0] == b[1] && b[1] == b[2] && b[2] == b[3]) ||
        b[0] == 0x00 || b[0] == 0xff) {
        return DECODE_FAIL_SANITY;
    }

    id          = (unsigned)b[0] << 20 | b[1] << 12 | b[2] << 4 | b[3] >> 4;
    flags       = b[3] & 0x0F;
    pressure    = b[4];
    temperature = b[5];

    if (temperature < 10 || temperature > 175) {
        return DECODE_FAIL_SANITY;
    }

    char id_str[7 + 1];
    snprintf(id_str, sizeof(id_str), "%07x", id);
    char code_str[7 * 2 + 1];
    snprintf(code_str, sizeof(code_str), "%02x%02x%02x%02x%02x%02x%02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6]);

    /* clang-format off */
    data_t *data = data_make(
            "model",            "",             DATA_STRING, "Jansite",
            "type",             "",             DATA_STRING, "TPMS",
            "id",               "",             DATA_STRING, id_str,
            "flags",            "",             DATA_INT, flags,
            "pressure_kPa",     "Pressure",     DATA_FORMAT, "%.0f kPa", DATA_DOUBLE, (double)pressure * 1.7,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.0f C", DATA_DOUBLE, (double)temperature - 50.0,
            "code",             "",             DATA_STRING, code_str,
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/** @sa tpms_jansite_decode() */
static int tpms_jansite_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern1[3] = {0xaa, 0xaa, 0xa9};
    uint8_t const preamble_pattern2[3] = {0x55, 0x55, 0x56};

    int ret    = 0;
    int events = 0;

    for (int row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, preamble_pattern1, 24)) + 136 <=
                bitbuffer->bits_per_row[row]) {
            ret = tpms_jansite_decode(decoder, bitbuffer, row, bitpos + 24);
            if (ret > 0)
                events += ret;
            bitpos += 2;
        }
        bitpos = 0;
        while ((bitpos = bitbuffer_search(bitbuffer, row, bitpos, preamble_pattern2, 24)) + 136 <=
                bitbuffer->bits_per_row[row]) {
            ret = tpms_jansite_decode(decoder, bitbuffer, row, bitpos + 24);
            if (ret > 0)
                events += ret;
            bitpos += 2;
        }
    }

    return events > 0 ? events : ret;
}

static char const *const output_fields[] = {
        "model",
        "type",
        "id",
        "flags",
        "pressure_kPa",
        "temperature_C",
        "code",
        NULL,
};

r_device const tpms_jansite = {
        .name        = "Jansite TPMS Model TY02S",
        .modulation  = FSK_PULSE_PCM,
        .short_width = 52,  // 12-13 samples @250k
        .long_width  = 52,  // FSK
        .reset_limit = 5000,
        .decode_fn   = &tpms_jansite_callback,
        .priority    = 10,
        .disabled    = 0,
        .fields      = output_fields,
};
