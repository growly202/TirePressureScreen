#ifndef GT911_REPORT_H
#define GT911_REPORT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    GT911_NO_DATA,
    GT911_RELEASED,
    GT911_PRESSED,
    GT911_INVALID,
} gt911_result_t;

/* Status at 0x814e followed by the first 8-byte touch record. Single pointer. */
gt911_result_t gt911_decode_report(const uint8_t *report, size_t size,
                                   uint16_t width, uint16_t height,
                                   uint16_t *x, uint16_t *y);

#endif
