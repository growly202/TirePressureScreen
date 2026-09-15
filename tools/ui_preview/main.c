#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_ui.h"

static uint16_t framebuffer[APP_WIDTH * APP_HEIGHT];
static uint16_t draw_buffer[APP_WIDTH * 40];
static unsigned flush_count;
static bool flush_failed;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    if (area->x1 < 0 || area->y1 < 0 || area->x2 >= APP_WIDTH || area->y2 >= APP_HEIGHT) {
        flush_failed = true;
    } else {
        size_t width = (size_t)(area->x2 - area->x1 + 1);
        for (int y = area->y1; y <= area->y2; ++y) {
            memcpy(&framebuffer[y * APP_WIDTH + area->x1], pixels, width * sizeof(uint16_t));
            pixels += width * sizeof(uint16_t);
        }
        ++flush_count;
    }
    lv_display_flush_ready(display);
}

static bool save_frame(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror(path);
        return false;
    }
    fprintf(file, "P6\n%d %d\n255\n", APP_WIDTH, APP_HEIGHT);
    for (size_t i = 0; i < APP_WIDTH * APP_HEIGHT; ++i) {
        uint16_t p = framebuffer[i];
        uint8_t rgb[] = {
            (uint8_t)(((p >> 11) & 31) * 255 / 31),
            (uint8_t)(((p >> 5) & 63) * 255 / 63),
            (uint8_t)((p & 31) * 255 / 31),
        };
        if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb)) {
            fclose(file);
            return false;
        }
    }
    bool ok = !ferror(file);
    return fclose(file) == 0 && ok;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "dashboard.ppm";
    lv_init();
    lv_display_t *display = lv_display_create(APP_WIDTH, APP_HEIGHT);
    if (!display) return EXIT_FAILURE;
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_t *screen = app_ui_start();
    if (!screen) return EXIT_FAILURE;

    /* Exercise the real LVGL timer/render path for 2 seconds of virtual time. */
    for (int i = 0; i < 400; ++i) {
        lv_tick_inc(5);
        lv_timer_handler();
    }
    const char *names[] = {"pressure_value_fl", "pressure_value_fr", "pressure_value_rl", "pressure_value_rr"};
    const char *values[] = {"2.31", "2.29", "2.34", "2.30"};
    for (size_t i = 0; i < 4; ++i) {
        lv_obj_t *label = lv_obj_find_by_name(screen, names[i]);
        if (!label || strcmp(lv_label_get_text(label), values[i]) != 0) {
            fprintf(stderr, "FAIL: missing or unexpected pressure label %s\n", names[i]);
            return EXIT_FAILURE;
        }
    }
    size_t different_pixels = 0;
    for (size_t i = 0; i < APP_WIDTH * APP_HEIGHT; ++i) {
        if (framebuffer[i] != framebuffer[0]) ++different_pixels;
    }
    if (flush_failed || flush_count == 0 || different_pixels < 10000) {
        fprintf(stderr, "FAIL: invalid/empty rendering (%u flushes, %zu changed pixels)\n", flush_count, different_pixels);
        return EXIT_FAILURE;
    }
    if (!save_frame(path)) return EXIT_FAILURE;
    printf("PASS: dashboard %dx%d; 4 pressure labels; %u flushes; %zu non-background pixels; 2000 ms virtual time\n",
           APP_WIDTH, APP_HEIGHT, flush_count, different_pixels);
    printf("Image: %s\n", path);
    lv_deinit();
    return EXIT_SUCCESS;
}
