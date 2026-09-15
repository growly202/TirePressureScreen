#include "gt911_report.h"

gt911_result_t gt911_decode_report(const uint8_t *report, size_t size,
                                   uint16_t width, uint16_t height,
                                   uint16_t *x, uint16_t *y)
{
    if (!report || !size || !x || !y) return GT911_INVALID;
    if (!(report[0] & 0x80)) return GT911_NO_DATA;
    unsigned count = report[0] & 0x0f;
    if (count > 5) return GT911_INVALID;
    if (count == 0) return GT911_RELEASED;
    if (size < 9) return GT911_INVALID;
    uint16_t px = report[2] | ((uint16_t)report[3] << 8);
    uint16_t py = report[4] | ((uint16_t)report[5] << 8);
    if (px >= width || py >= height) return GT911_INVALID;
    *x = px;
    *y = py;
    return GT911_PRESSED;
}
