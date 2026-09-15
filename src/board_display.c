/* Waveshare ESP32-S3-Touch-LCD-7 (800x480, ST7262 / GT911 / CH422G).
 * Pin/timing reference: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-7
 * Uses the ESP-IDF 6 I2C API; no legacy I2C driver or Arduino dependency.
 */
#include "board_display.h"
#include "app_ui.h"
#include "gt911_report.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_TIMEOUT_MS 50
#define TOUCH_INT GPIO_NUM_4
#define EXIO_TOUCH_RESET (1U << 1)
#define EXIO_BACKLIGHT (1U << 2)
#define EXIO_LCD_RESET (1U << 3)
#define EXIO_SD_CS (1U << 4)
/* EXIO5 must remain LOW for USB (HIGH selects CAN). */

static const char *TAG = "waveshare_lcd";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t expander_config, expander_output, touch;
static esp_lcd_panel_handle_t panel;
static uint8_t outputs;
static uint32_t flushes;
static lv_point_t touch_point;
static bool touch_pressed, touch_io_failed;

static esp_err_t add_device(uint16_t address, i2c_master_dev_handle_t *device)
{
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus, &config, device);
}

static esp_err_t set_outputs(uint8_t value)
{
    esp_err_t err = i2c_master_transmit(expander_output, &value, 1, I2C_TIMEOUT_MS);
    if (err == ESP_OK) outputs = value;
    return err;
}

static esp_err_t touch_read_register(uint16_t reg, uint8_t *data, size_t size)
{
    uint8_t address[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_transmit_receive(touch, address, sizeof(address), data, size, I2C_TIMEOUT_MS);
}

static esp_err_t reset_devices(void)
{
    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&config, &bus), TAG, "I2C init");
    ESP_RETURN_ON_ERROR(add_device(0x24, &expander_config), TAG, "CH422G config handle");
    ESP_RETURN_ON_ERROR(add_device(0x38, &expander_output), TAG, "CH422G output handle");

    gpio_config_t int_config = {
        .pin_bit_mask = 1ULL << TOUCH_INT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_config), TAG, "GT911 INT GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(TOUCH_INT, 0), TAG, "GT911 address select");
    /* Preload outputs before enabling them: resets LOW, backlight OFF,
     * SD deselected, USB selected. CH422G has command addresses, not registers. */
    ESP_RETURN_ON_ERROR(set_outputs(EXIO_SD_CS), TAG, "CH422G initial output");
    uint8_t mode = 0x01;
    ESP_RETURN_ON_ERROR(i2c_master_transmit(expander_config, &mode, 1, I2C_TIMEOUT_MS), TAG, "CH422G output mode");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(set_outputs(EXIO_SD_CS | EXIO_LCD_RESET), TAG, "LCD reset release");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(set_outputs(EXIO_SD_CS | EXIO_LCD_RESET | EXIO_TOUCH_RESET), TAG, "GT911 reset release");
    /* INT LOW during reset selects 7-bit address 0x5d. Release INT afterward. */
    vTaskDelay(pdMS_TO_TICKS(60));
    ESP_RETURN_ON_ERROR(gpio_set_direction(TOUCH_INT, GPIO_MODE_INPUT), TAG, "Release GT911 INT");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

static void lcd_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    /* IDF 6 RGB driver copies this separate partial buffer synchronously into
     * its PSRAM framebuffer. It is safe to release it after draw_bitmap returns.
     * This does not mean the panel has finished scanning that frame. */
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1,
                                             area->x2 + 1, area->y2 + 1, pixels));
    ++flushes;
    lv_display_flush_ready(display);
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint8_t report[9] = {0};
    esp_err_t err = touch_read_register(0x814e, report, 1);
    if (err == ESP_OK && (report[0] & 0x80)) {
        unsigned count = report[0] & 0x0f;
        if (count > 0 && count <= 5) {
            err = touch_read_register(0x814f, &report[1], 8);
        }
        /* Acknowledge even malformed packets; never leave ready stuck. */
        const uint8_t clear[] = {0x81, 0x4e, 0x00};
        esp_err_t ack = i2c_master_transmit(touch, clear, sizeof(clear), I2C_TIMEOUT_MS);
        if (err == ESP_OK) err = ack;
    }
    if (err != ESP_OK) {
        if (!touch_io_failed) ESP_LOGW(TAG, "Touch I2C error: %s", esp_err_to_name(err));
        touch_io_failed = true;
        touch_pressed = false;
    } else {
        touch_io_failed = false;
        uint16_t x, y;
        gt911_result_t result = gt911_decode_report(report, sizeof(report), APP_WIDTH, APP_HEIGHT, &x, &y);
        if (result == GT911_PRESSED) {
            if (!touch_pressed) ESP_LOGI(TAG, "Touch: x=%u y=%u", (unsigned)x, (unsigned)y);
            touch_point.x = x;
            touch_point.y = y;
            touch_pressed = true;
        } else if (result != GT911_NO_DATA) {
            touch_pressed = false;
        }
    }
    data->point = touch_point;
    data->state = touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static esp_err_t init_touch(lv_display_t *display)
{
    ESP_RETURN_ON_ERROR(add_device(0x5d, &touch), TAG, "GT911 handle");
    uint8_t id[4];
    ESP_RETURN_ON_ERROR(touch_read_register(0x8140, id, sizeof(id)), TAG, "GT911 product ID");
    ESP_RETURN_ON_FALSE(id[0] == '9' && id[1] == '1' && id[2] == '1',
                        ESP_ERR_NOT_SUPPORTED, TAG, "Unexpected touch controller");
    lv_indev_t *input = lv_indev_create();
    ESP_RETURN_ON_FALSE(input, ESP_ERR_NO_MEM, TAG, "LVGL input allocation");
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_read_cb(input, touch_read);
    ESP_LOGI(TAG, "GT911 ready at 0x5d (single pointer, polling)");
    return ESP_OK;
}

esp_err_t board_display_init(lv_display_t **display)
{
    ESP_RETURN_ON_FALSE(display && !panel, ESP_ERR_INVALID_STATE, TAG, "Display init is one-shot");
    *display = NULL;
    ESP_RETURN_ON_FALSE(esp_psram_is_initialized(), ESP_ERR_INVALID_STATE, TAG, "PSRAM unavailable");
    ESP_LOGI(TAG, "PSRAM: %u bytes", (unsigned)esp_psram_get_size());
    ESP_RETURN_ON_ERROR(reset_devices(), TAG, "Board reset failed");

    esp_lcd_rgb_panel_config_t config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 16000000,
            .h_res = APP_WIDTH, .v_res = APP_HEIGHT,
            .hsync_pulse_width = 4, .hsync_back_porch = 8, .hsync_front_porch = 8,
            .vsync_pulse_width = 4, .vsync_back_porch = 8, .vsync_front_porch = 8,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1,
        .bounce_buffer_size_px = APP_WIDTH * 10,
        .dma_burst_size = 64,
        .hsync_gpio_num = GPIO_NUM_46, .vsync_gpio_num = GPIO_NUM_3,
        .de_gpio_num = GPIO_NUM_5, .pclk_gpio_num = GPIO_NUM_7,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
        .flags.fb_in_psram = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&config, &panel), TAG, "RGB panel allocation");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "RGB peripheral reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "RGB panel init");
    const size_t buffer_size = APP_WIDTH * 20 * sizeof(uint16_t);
    void *buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(buffer, ESP_ERR_NO_MEM, TAG, "LVGL draw buffer");
    *display = lv_display_create(APP_WIDTH, APP_HEIGHT);
    if (!*display) {
        heap_caps_free(buffer);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(*display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(*display, buffer, NULL, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(*display, lcd_flush);
    /* A missing touch controller should not prevent viewing the dashboard. */
    esp_err_t touch_err = init_touch(*display);
    if (touch_err != ESP_OK) ESP_LOGW(TAG, "Touch disabled: %s", esp_err_to_name(touch_err));
    ESP_LOGI(TAG, "RGB LCD ready: 800x480 RGB565, PCLK 16 MHz");
    return ESP_OK;
}

esp_err_t board_backlight_on(void)
{
    return set_outputs(outputs | EXIO_BACKLIGHT);
}

uint32_t board_display_flush_count(void)
{
    return flushes;
}
