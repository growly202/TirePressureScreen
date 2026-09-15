#include <stdio.h>
#include <stdlib.h>
#include "gt911_report.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void)
{
    uint16_t x = 123, y = 456;
    /* First point at (799,479): little-endian coordinates, not track ID. */
    uint8_t report[] = {0x81, 3, 0x1f, 0x03, 0xdf, 0x01, 0x20, 0x00, 0x00};
    CHECK(gt911_decode_report(report, sizeof(report), 800, 480, &x, &y) == GT911_PRESSED);
    CHECK(x == 799 && y == 479);
    report[0] = 0x80;
    CHECK(gt911_decode_report(report, 1, 800, 480, &x, &y) == GT911_RELEASED);
    CHECK(x == 799 && y == 479);
    report[0] = 0x00;
    CHECK(gt911_decode_report(report, 1, 800, 480, &x, &y) == GT911_NO_DATA);
    report[0] = 0x86;
    CHECK(gt911_decode_report(report, sizeof(report), 800, 480, &x, &y) == GT911_INVALID);
    report[0] = 0x81;
    CHECK(gt911_decode_report(report, 8, 800, 480, &x, &y) == GT911_INVALID);
    report[2] = 0x20; /* x=800 is outside the panel. */
    CHECK(gt911_decode_report(report, sizeof(report), 800, 480, &x, &y) == GT911_INVALID);
    report[2] = report[3] = report[4] = report[5] = 0;
    CHECK(gt911_decode_report(report, sizeof(report), 800, 480, &x, &y) == GT911_PRESSED);
    CHECK(x == 0 && y == 0);
    report[0] = 0x85; /* Five reported contacts: use the first for LVGL. */
    CHECK(gt911_decode_report(report, sizeof(report), 800, 480, &x, &y) == GT911_PRESSED);
    CHECK(gt911_decode_report(NULL, 0, 800, 480, &x, &y) == GT911_INVALID);
    CHECK(gt911_decode_report(report, sizeof(report), 800, 480, NULL, &y) == GT911_INVALID);
    puts("PASS: GT911 press/release/no-data, little-endian coordinates, bounds, malformed reports, single-pointer policy");
    return EXIT_SUCCESS;
}
