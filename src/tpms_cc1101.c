#include "tpms_cc1101.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "board_display.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "tpms_espnow.h"

/* rtl_433 C core headers */
#include "bitbuffer.h"
#include "data.h"
#include "pulse_data.h"
#include "r_api.h"
#include "r_device.h"
#include "r_private.h"
#include "rtl_433.h"
#include "rtl_433_devices.h"

static const char *TAG = "tpms_cc1101";

/* CC1101 Command Strobes */
#define CC1101_CMD_READ              0x80
#define CC1101_CMD_BURST             0x40
#define CC1101_CMD_STATUS_REG        0xC0
#define CC1101_CMD_RESET             0x30
#define CC1101_CMD_RX                0x34
#define CC1101_CMD_IDLE              0x36
#define CC1101_CMD_FLUSH_RX          0x3A
#define CC1101_CMD_FLUSH_TX          0x3B

/* CC1101 Configuration Registers */
#define CC1101_REG_IOCFG2            0x00
#define CC1101_REG_IOCFG1            0x01
#define CC1101_REG_IOCFG0            0x02
#define CC1101_REG_FIFOTHR           0x03
#define CC1101_REG_SYNC1             0x04
#define CC1101_REG_SYNC0             0x05
#define CC1101_REG_PKTLEN            0x06
#define CC1101_REG_PKTCTRL1          0x07
#define CC1101_REG_PKTCTRL0          0x08
#define CC1101_REG_ADDR              0x09
#define CC1101_REG_CHANNR            0x0A
#define CC1101_REG_FSCTRL1           0x0B
#define CC1101_REG_FSCTRL0           0x0C
#define CC1101_REG_FREQ2             0x0D
#define CC1101_REG_FREQ1             0x0E
#define CC1101_REG_FREQ0             0x0F
#define CC1101_REG_MDMCFG4           0x10
#define CC1101_REG_MDMCFG3           0x11
#define CC1101_REG_MDMCFG2           0x12
#define CC1101_REG_MDMCFG1           0x13
#define CC1101_REG_MDMCFG0           0x14
#define CC1101_REG_DEVIATN           0x15
#define CC1101_REG_MCSM2             0x16
#define CC1101_REG_MCSM1             0x17
#define CC1101_REG_MCSM0             0x18
#define CC1101_REG_FOCCFG            0x19
#define CC1101_REG_BSCFG             0x1A
#define CC1101_REG_AGCCTRL2          0x1B
#define CC1101_REG_AGCCTRL1          0x1C
#define CC1101_REG_AGCCTRL0          0x1D
#define CC1101_REG_WOREVT1           0x1E
#define CC1101_REG_WOREVT0           0x1F
#define CC1101_REG_WORCTRL           0x20
#define CC1101_REG_FREND1            0x21
#define CC1101_REG_FREND0            0x22
#define CC1101_REG_FSCAL3            0x23
#define CC1101_REG_FSCAL2            0x24
#define CC1101_REG_FSCAL1            0x25
#define CC1101_REG_FSCAL0            0x26
#define CC1101_REG_RCCTRL1           0x27
#define CC1101_REG_RCCTRL0           0x28
#define CC1101_REG_PATABLE           0x3E

/* CC1101 Status Registers (accessed with 0xC0) */
#define CC1101_REG_PARTNUM           0x30
#define CC1101_REG_VERSION           0x31
#define CC1101_REG_RSSI              0x34
#define CC1101_REG_MARCSTATE         0x35
#define CC1101_REG_PKTSTATUS         0x38
#define CC1101_REG_RXBYTES           0x3B

/* OOK_Receiver / rtl_433_ESP capture parameters */
#define RECEIVER_BUFFER_SIZE         4
#define PRETRIGGER_WINDOW_US         8000U
#define PRETRIGGER_PAIRS             128U
#define MINIMUM_PULSE_LENGTH_US      25U
#define OOK_MIN_SIGNAL_LENGTH_US     5000UL
#define FSK_MIN_SIGNAL_LENGTH_US     500UL
#define OOK_INTERFRAME_GAP_US        25000UL
#define FSK_INTERFRAME_GAP_US        6000UL
#define RSSI_INITIAL_SAMPLES         1000
#define RSSI_PERIODIC_SAMPLES        50000
#define RSSI_THRESHOLD_DELTA_DB      9
#define MIN_RSSI_DEFAULT_DBM         (-82)
#define JSON_MSG_BUFFER_SIZE         512
#define MODULATION_SCAN_INTERVAL_MS  1500U

static bool s_initialized = false;
static SemaphoreHandle_t s_spi_mutex = NULL;
static QueueHandle_t s_decoder_queue = NULL;
static portMUX_TYPE s_capture_mux = portMUX_INITIALIZER_UNLOCKED;

static gpio_num_t s_cs_gpio = TPMS_CC1101_CS_GPIO;
static gpio_num_t s_miso_gpio = TPMS_CC1101_MISO_GPIO;
static gpio_num_t s_mosi_gpio = TPMS_CC1101_MOSI_GPIO;
static gpio_num_t s_sclk_gpio = TPMS_CC1101_SCLK_GPIO;

static r_cfg_t s_rtl_cfg;
static char s_json_buffer[JSON_MSG_BUFFER_SIZE];

static pulse_data_t *s_pulse_trains = NULL;
static volatile uint8_t s_actual_train = 0;
static uint8_t s_available_train = 0;
static volatile int16_t s_nrpulses = 0;
static volatile uint32_t s_last_change_us = 0;

static volatile unsigned int s_pretrigger_pulse[PRETRIGGER_PAIRS];
static volatile unsigned int s_pretrigger_gap[PRETRIGGER_PAIRS];
static volatile uint8_t s_pretrigger_head = 0;
static volatile uint8_t s_pretrigger_count = 0;
static volatile unsigned int s_pretrigger_pending_pulse = 0;

static volatile bool s_enabled_receiver = false;
static volatile bool s_receive_mode = false;
static volatile bool s_cc1101_online = false;
static volatile bool s_ook_modulation = true;
static volatile bool s_auto_scan_modulation = false;

static volatile int s_current_rssi = MIN_RSSI_DEFAULT_DBM;
static volatile int s_signal_rssi = MIN_RSSI_DEFAULT_DBM;
static volatile int s_rssi_threshold = MIN_RSSI_DEFAULT_DBM;
static int s_average_rssi = MIN_RSSI_DEFAULT_DBM;
static bool s_rssi_calibrated = false;
static int s_total_rssi = 0;
static int s_rssi_count = 0;
static int s_peak_rssi = -256;
static int s_above_threshold = 0;

static uint32_t s_signal_start_us = 0;
static uint32_t s_signal_end_us = 0;
static uint32_t s_total_signals = 0;
static uint32_t s_ignored_signals = 0;
static uint32_t s_decoded_messages = 0;
static uint32_t s_decode_seq = 0;
static volatile uint32_t s_gdo0_edges = 0;
static volatile uint32_t s_gdo0_active_edges = 0;

typedef struct {
    bool valid;
    tpms_espnow_packet_t pkt;
    char flags_str[12];
    char code_str[24];
    char raw_json[256];
} staged_tpms_decode_t;

static staged_tpms_decode_t s_staged_decode;

/* Required by rtl_433 C core (r_api.c / log.h) */
unsigned long rtl_433_millis(void)
{
    return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

static inline uint32_t IRAM_ATTR micros_now(void)
{
    return (uint32_t)esp_timer_get_time();
}

static inline int IRAM_ATTR gdo0_get_level_fast(void)
{
    return (int)((GPIO.in >> (uint32_t)TPMS_CC1101_GDO0_GPIO) & 0x1U);
}

static void cc1101_configure_uart2_pins(gpio_num_t cs_pin, gpio_num_t miso_pin)
{
    s_cs_gpio = cs_pin;
    s_miso_gpio = miso_pin;

    gpio_reset_pin(s_cs_gpio);
    gpio_set_level(s_cs_gpio, 1);
    gpio_config_t cs_cfg = {
        .pin_bit_mask = 1ULL << s_cs_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&cs_cfg);
    gpio_set_level(s_cs_gpio, 1);

    gpio_reset_pin(s_miso_gpio);
    gpio_config_t miso_cfg = {
        .pin_bit_mask = 1ULL << s_miso_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&miso_cfg);
}

/* Acquire shared I2C pins (GPIO8/9) for an SPI Mode 0 transaction
 * without ever generating an I2C START or STOP condition (SDA only changes while SCL=0). */
static void cc1101_spi_begin_locked(void)
{
    board_i2c_lock();

    /* Park ESP32-S3 I2C0 peripheral inputs at logic HIGH while GPIO8/9 run SPI Mode 0 */
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, I2CEXT0_SDA_IN_IDX, false);
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, I2CEXT0_SCL_IN_IDX, false);

    /* Disconnect I2C peripheral outputs from GPIO8/9 and connect to GPIO output register */
    esp_rom_gpio_connect_out_signal(GPIO_NUM_8, SIG_GPIO_OUT_IDX, false, false);
    esp_rom_gpio_connect_out_signal(GPIO_NUM_9, SIG_GPIO_OUT_IDX, false, false);

    /* Explicitly enable push-pull output drivers on GPIO8 and GPIO9 */
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_9, GPIO_MODE_OUTPUT);

    /* Both idle HIGH in I2C state */
    gpio_set_level(GPIO_NUM_8, 1);
    gpio_set_level(GPIO_NUM_9, 1);
    esp_rom_delay_us(5);

    /* Pull SCLK LOW first while MOSI is HIGH -> no I2C START condition! */
    gpio_set_level(s_sclk_gpio, 0);
    esp_rom_delay_us(5);

    /* Assert CSN LOW and wait for CC1101 MISO (SO) to go LOW (CHIP_RDYn) */
    gpio_set_level(s_cs_gpio, 0);
    for (int wait = 0; wait < 200 && gpio_get_level(s_miso_gpio) != 0; ++wait) {
        esp_rom_delay_us(2);
    }
    esp_rom_delay_us(2);
}

static void cc1101_spi_end_locked(void)
{
    /* Deassert CSN HIGH while SCLK is still LOW */
    gpio_set_level(s_cs_gpio, 1);
    esp_rom_delay_us(5);

    /* Return MOSI HIGH while SCLK is still LOW, then raise SCLK HIGH */
    gpio_set_level(s_mosi_gpio, 1);
    esp_rom_delay_us(5);
    gpio_set_level(s_sclk_gpio, 1);
    esp_rom_delay_us(5);

    /* Restore GPIO8/9 to open-drain mode and reconnect to ESP32-S3 I2C0 peripheral */
    gpio_set_direction(GPIO_NUM_8, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(GPIO_NUM_9, GPIO_MODE_INPUT_OUTPUT_OD);

    esp_rom_gpio_connect_out_signal(GPIO_NUM_8, I2CEXT0_SDA_OUT_IDX, false, false);
    esp_rom_gpio_connect_out_signal(GPIO_NUM_9, I2CEXT0_SCL_OUT_IDX, false, false);
    esp_rom_gpio_connect_in_signal(GPIO_NUM_8, I2CEXT0_SDA_IN_IDX, false);
    esp_rom_gpio_connect_in_signal(GPIO_NUM_9, I2CEXT0_SCL_IN_IDX, false);

    board_i2c_unlock();
}

static uint8_t cc1101_spi_xfer_byte_raw(uint8_t tx)
{
    uint8_t rx = 0;
    for (int bit = 7; bit >= 0; --bit) {
        /* SCLK is LOW here: update MOSI and allow 4.7k pull-up through M1 to settle */
        gpio_set_level(s_mosi_gpio, (tx >> bit) & 1);
        esp_rom_delay_us(5);

        /* Rising edge of SCLK: allow 4.7k pull-up on SCL through M1 to reach HIGH */
        gpio_set_level(s_sclk_gpio, 1);
        esp_rom_delay_us(6);
        rx = (uint8_t)((rx << 1) | (gpio_get_level(s_miso_gpio) ? 1U : 0U));

        /* Falling edge of SCLK (actively pulled low through M1) */
        gpio_set_level(s_sclk_gpio, 0);
        esp_rom_delay_us(3);
    }
    return rx;
}

static esp_err_t cc1101_spi_strobe(uint8_t cmd)
{
    cc1101_spi_begin_locked();
    (void)cc1101_spi_xfer_byte_raw(cmd);
    cc1101_spi_end_locked();
    return ESP_OK;
}

static void cc1101_manual_reset(void)
{
    /* CC1101 Section 10.1 Manual Reset sequence */
    gpio_set_level(s_cs_gpio, 1);
    esp_rom_delay_us(30);
    gpio_set_level(s_cs_gpio, 0);
    esp_rom_delay_us(30);
    gpio_set_level(s_cs_gpio, 1);
    esp_rom_delay_us(45);

    cc1101_spi_strobe(CC1101_CMD_RESET);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static esp_err_t cc1101_spi_write_reg(uint8_t reg, uint8_t value)
{
    cc1101_spi_begin_locked();
    (void)cc1101_spi_xfer_byte_raw((uint8_t)(reg & 0x3F));
    (void)cc1101_spi_xfer_byte_raw(value);
    cc1101_spi_end_locked();
    return ESP_OK;
}

static uint8_t cc1101_spi_read_reg(uint8_t reg)
{
    uint8_t header = (reg >= 0x30 && reg <= 0x3D)
                         ? (uint8_t)(reg | CC1101_CMD_STATUS_REG)
                         : (uint8_t)((reg & 0x3F) | CC1101_CMD_READ);
    cc1101_spi_begin_locked();
    (void)cc1101_spi_xfer_byte_raw(header);
    uint8_t val = cc1101_spi_xfer_byte_raw(0x00);
    cc1101_spi_end_locked();
    return val;
}

static int cc1101_raw_to_rssi_dbm(uint8_t raw)
{
    if (raw >= 128) {
        return (((int)raw - 256) / 2) - 74;
    }
    return ((int)raw / 2) - 74;
}

static int cc1101_median3(int a, int b, int c)
{
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        b = c;
    }
    return (a > b) ? a : b;
}

/* CC1101 Silicon Errata SWRZ020E: status register reads while in RX mode can
 * return a transient corrupted byte when an internal RSSI ADC update coincides
 * with an SPI clock edge. Fast-path idle readsbelow threshold with 1 transfer;
 * verify above-threshold / active-burst reads with a 2nd read (and median-of-3 on mismatch). */
static int cc1101_read_rssi_dbm(void)
{
    int r1 = cc1101_raw_to_rssi_dbm(cc1101_spi_read_reg(CC1101_REG_RSSI));
    if (!s_receive_mode && r1 <= s_rssi_threshold) {
        return r1;
    }
    int r2 = cc1101_raw_to_rssi_dbm(cc1101_spi_read_reg(CC1101_REG_RSSI));
    if (abs(r1 - r2) <= 2) {
        return r2;
    }
    int r3 = cc1101_raw_to_rssi_dbm(cc1101_spi_read_reg(CC1101_REG_RSSI));
    return cc1101_median3(r1, r2, r3);
}

static void cc1101_set_frequency_regs(float freq_mhz)
{
    /* FRF = (freq_mhz * 2^16) / 26.0 */
    uint32_t frf = (uint32_t)((freq_mhz * 65536.0f) / 26.0f);
    cc1101_spi_write_reg(CC1101_REG_FREQ2, (uint8_t)((frf >> 16) & 0xFF));
    cc1101_spi_write_reg(CC1101_REG_FREQ1, (uint8_t)((frf >> 8) & 0xFF));
    cc1101_spi_write_reg(CC1101_REG_FREQ0, (uint8_t)(frf & 0xFF));
}

static void cc1101_apply_ook_profile_locked(void)
{
    cc1101_spi_strobe(CC1101_CMD_IDLE);
    vTaskDelay(pdMS_TO_TICKS(1));

    cc1101_spi_write_reg(CC1101_REG_PKTLEN, 0x00);
    cc1101_spi_write_reg(CC1101_REG_PKTCTRL1, 0x04);
    /* Async continuous mode, CRC off, infinite packet length (0x32) */
    cc1101_spi_write_reg(CC1101_REG_PKTCTRL0, 0x32);

    /* Match RadioLib CC1101::begin() defaults for preamble & sync word */
    cc1101_spi_write_reg(CC1101_REG_MDMCFG1, 0x02);
    cc1101_spi_write_reg(CC1101_REG_SYNC1, 0x12);
    cc1101_spi_write_reg(CC1101_REG_SYNC0, 0xAD);

    /* ASK/OOK modulation (0x30), Manchester off, no preamble/sync word (0x00) */
    cc1101_spi_write_reg(CC1101_REG_MDMCFG2, 0x30);
    /* RX filter BW = 812.5 kHz (CHANBW_E=0, CHANBW_M=0), DRATE_E = 7 (~5.0 kBaud) */
    cc1101_spi_write_reg(CC1101_REG_MDMCFG4, 0x07);
    cc1101_spi_write_reg(CC1101_REG_MDMCFG3, 0x93);

    /* AGC settings from rtl_433_ESP / lsatan for 433.92 MHz OOK */
    cc1101_spi_write_reg(CC1101_REG_AGCCTRL2, 0xC7);
    cc1101_spi_write_reg(CC1101_REG_AGCCTRL1, 0x40);
    cc1101_spi_write_reg(CC1101_REG_AGCCTRL0, 0xB2);

    cc1101_spi_write_reg(CC1101_REG_FREND1, 0x56);
    cc1101_spi_write_reg(CC1101_REG_FREND0, 0x11);

    /* Write 8-byte PATABLE in burst mode (0x3E | 0x40) */
    const uint8_t patable[8] = {0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    cc1101_spi_begin_locked();
    (void)cc1101_spi_xfer_byte_raw(CC1101_REG_PATABLE | CC1101_CMD_BURST);
    for (int i = 0; i < 8; ++i) {
        (void)cc1101_spi_xfer_byte_raw(patable[i]);
    }
    cc1101_spi_end_locked();

    /* Route async demodulator data out to both GDO0 and GDO2 */
    cc1101_spi_write_reg(CC1101_REG_IOCFG0, 0x0D);
    cc1101_spi_write_reg(CC1101_REG_IOCFG2, 0x0D);

    cc1101_spi_strobe(CC1101_CMD_FLUSH_RX);
    cc1101_spi_strobe(CC1101_CMD_FLUSH_TX);
    cc1101_spi_strobe(CC1101_CMD_RX);
}

static void cc1101_apply_fsk_profile_locked(void)
{
    cc1101_spi_strobe(CC1101_CMD_IDLE);
    vTaskDelay(pdMS_TO_TICKS(1));

    cc1101_spi_write_reg(CC1101_REG_PKTLEN, 0x00);
    cc1101_spi_write_reg(CC1101_REG_PKTCTRL1, 0x04);
    cc1101_spi_write_reg(CC1101_REG_PKTCTRL0, 0x32);

    /* 2-FSK modulation (0x00), Manchester off, no preamble/sync word (0x00) */
    cc1101_spi_write_reg(CC1101_REG_MDMCFG2, 0x00);
    /* RX filter BW = 325 kHz (0x50), BitRate = 20.0 kbps (DRATE_E=9, DRATE_M=0x93) */
    cc1101_spi_write_reg(CC1101_REG_MDMCFG4, 0x59);
    cc1101_spi_write_reg(CC1101_REG_MDMCFG3, 0x93);
    /* Frequency deviation = 40 kHz (DEVIATN = 0x44) */
    cc1101_spi_write_reg(CC1101_REG_DEVIATN, 0x44);
    cc1101_spi_write_reg(CC1101_REG_FREND0, 0x10);

    cc1101_spi_write_reg(CC1101_REG_IOCFG0, 0x0D);
    cc1101_spi_write_reg(CC1101_REG_IOCFG2, 0x0D);

    cc1101_spi_strobe(CC1101_CMD_FLUSH_RX);
    cc1101_spi_strobe(CC1101_CMD_FLUSH_TX);
    cc1101_spi_strobe(CC1101_CMD_RX);
}

static void cc1101_log_status_locked(void)
{
    int rssi_now = cc1101_read_rssi_dbm();
    int gdo0_lvl = gpio_get_level(TPMS_CC1101_GDO0_GPIO);
    ESP_LOGI(TAG, "----- CC1101 Status -----");
    ESP_LOGI(TAG, "CC1101_PARTNUM:  0x%02x  VERSION:   0x%02x  MARCSTATE: 0x%02x  PKTSTATUS: 0x%02x",
             cc1101_spi_read_reg(CC1101_REG_PARTNUM),
             cc1101_spi_read_reg(CC1101_REG_VERSION),
             cc1101_spi_read_reg(CC1101_REG_MARCSTATE),
             cc1101_spi_read_reg(CC1101_REG_PKTSTATUS));
    ESP_LOGI(TAG, "CC1101_MDMCFG1:  0x%02x  MDMCFG2:   0x%02x  MDMCFG3:   0x%02x  MDMCFG4: 0x%02x",
             cc1101_spi_read_reg(CC1101_REG_MDMCFG1),
             cc1101_spi_read_reg(CC1101_REG_MDMCFG2),
             cc1101_spi_read_reg(CC1101_REG_MDMCFG3),
             cc1101_spi_read_reg(CC1101_REG_MDMCFG4));
    ESP_LOGI(TAG, "CC1101_AGCCTRL0: 0x%02x  AGCCTRL1:  0x%02x  AGCCTRL2:  0x%02x  DEVIATN: 0x%02x",
             cc1101_spi_read_reg(CC1101_REG_AGCCTRL0),
             cc1101_spi_read_reg(CC1101_REG_AGCCTRL1),
             cc1101_spi_read_reg(CC1101_REG_AGCCTRL2),
             cc1101_spi_read_reg(CC1101_REG_DEVIATN));
    ESP_LOGI(TAG, "CC1101_IOCFG0:   0x%02x  IOCFG2:    0x%02x  PKTCTRL0:  0x%02x  PKTCTRL1:0x%02x",
             cc1101_spi_read_reg(CC1101_REG_IOCFG0),
             cc1101_spi_read_reg(CC1101_REG_IOCFG2),
             cc1101_spi_read_reg(CC1101_REG_PKTCTRL0),
             cc1101_spi_read_reg(CC1101_REG_PKTCTRL1));
    ESP_LOGI(TAG, "CC1101_FREQ2:    0x%02x  FREQ1:     0x%02x  FREQ0:     0x%02x  FSCAL3..0: %02x/%02x/%02x/%02x",
             cc1101_spi_read_reg(CC1101_REG_FREQ2),
             cc1101_spi_read_reg(CC1101_REG_FREQ1),
             cc1101_spi_read_reg(CC1101_REG_FREQ0),
             cc1101_spi_read_reg(CC1101_REG_FSCAL3),
             cc1101_spi_read_reg(CC1101_REG_FSCAL2),
             cc1101_spi_read_reg(CC1101_REG_FSCAL1),
             cc1101_spi_read_reg(CC1101_REG_FSCAL0));
    ESP_LOGI(TAG, "RF Live: mod=%s cal=%d rssi=%d dBm avg=%d dBm thresh=%d dBm peak=%d dBm above=%d",
             s_ook_modulation ? "OOK" : "FSK",
             (int)s_rssi_calibrated, rssi_now, s_average_rssi, s_rssi_threshold, s_peak_rssi, s_above_threshold);
    ESP_LOGI(TAG, "GDO0(GPIO%d): level=%d total_edges=%" PRIu32 " active_edges=%" PRIu32 " pretrig_cnt=%u",
             (int)TPMS_CC1101_GDO0_GPIO, gdo0_lvl, s_gdo0_edges, s_gdo0_active_edges, (unsigned)s_pretrigger_count);
    ESP_LOGI(TAG, "Capture: signals=%" PRIu32 " ignored=%" PRIu32 " decoded=%" PRIu32 " rx_mode=%d pulses=%d",
             s_total_signals, s_ignored_signals, s_decoded_messages, (int)s_receive_mode, (int)s_nrpulses);
    ESP_LOGI(TAG, "-------------------------");
}

static bool cc1101_probe_candidate_locked(gpio_num_t cs_pin, gpio_num_t miso_pin,
                                          gpio_num_t mosi_pin, gpio_num_t sclk_pin)
{
    s_mosi_gpio = mosi_pin;
    s_sclk_gpio = sclk_pin;
    cc1101_configure_uart2_pins(cs_pin, miso_pin);

    cc1101_manual_reset();

    uint8_t version = cc1101_spi_read_reg(CC1101_REG_VERSION);
    uint8_t partnum = cc1101_spi_read_reg(CC1101_REG_PARTNUM);
    uint8_t iocfg2 = cc1101_spi_read_reg(CC1101_REG_IOCFG2);
    int miso_lvl = gpio_get_level(miso_pin);
    int mosi_lvl = gpio_get_level(mosi_pin);
    int sclk_lvl = gpio_get_level(sclk_pin);

    ESP_LOGI(TAG, "Probe CS=GPIO%d MISO=GPIO%d MOSI=GPIO%d SCLK=GPIO%d -> MISO=%d, MOSI=%d, SCLK=%d, IOCFG2=0x%02X, PARTNUM=0x%02X, VERSION=0x%02X",
             (int)cs_pin, (int)miso_pin, (int)mosi_pin, (int)sclk_pin,
             miso_lvl, mosi_lvl, sclk_lvl, iocfg2, partnum, version);

    /* Valid CC1101 chips report VERSION 0x14, 0x04, 0x07, etc. (never 0x00 or 0xFF). */
    if (version == 0x00 || version == 0xFF || partnum == 0xFF) {
        return false;
    }

    /* Enable auto-calibration from IDLE to RX/TX (MCSM0 = 0x18) */
    cc1101_spi_strobe(CC1101_CMD_IDLE);
    cc1101_spi_write_reg(CC1101_REG_MCSM0, 0x18);
    cc1101_set_frequency_regs(TPMS_CC1101_FREQ_MHZ);

    if (s_ook_modulation) {
        cc1101_apply_ook_profile_locked();
    } else {
        cc1101_apply_fsk_profile_locked();
    }

    /* Verify that PKTCTRL0 readback matches 0x32 to confirm reliable SPI link */
    uint8_t pktctrl0 = cc1101_spi_read_reg(CC1101_REG_PKTCTRL0);
    if (pktctrl0 != 0x32) {
        return false;
    }

    ESP_LOGI(TAG,
             "CC1101 detected! PARTNUM=0x%02X VERSION=0x%02X at %.2f MHz (%s) [CSN=GPIO%d, MISO=GPIO%d, MOSI=GPIO%d, SCLK=GPIO%d, GDO0=GPIO%d]",
             partnum, version, (double)TPMS_CC1101_FREQ_MHZ,
             s_ook_modulation ? "OOK" : "FSK",
             (int)s_cs_gpio, (int)s_miso_gpio,
             (int)s_mosi_gpio, (int)s_sclk_gpio, (int)TPMS_CC1101_GDO0_GPIO);
    cc1101_log_status_locked();
    return true;
}

static bool cc1101_probe_and_configure(void)
{
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);

    /* 1. Normal: CS=TXD(43), MISO=RXD(44), MOSI=SDA(8), SCLK=SCL(9) */
    bool ok = cc1101_probe_candidate_locked(TPMS_CC1101_CS_GPIO, TPMS_CC1101_MISO_GPIO,
                                            GPIO_NUM_8, GPIO_NUM_9);
    /* 2. Swapped MOSI/SCLK: CS=TXD(43), MISO=RXD(44), MOSI=SCL(9), SCLK=SDA(8) */
    if (!ok) {
        ok = cc1101_probe_candidate_locked(TPMS_CC1101_CS_GPIO, TPMS_CC1101_MISO_GPIO,
                                            GPIO_NUM_9, GPIO_NUM_8);
    }
    /* 3. Swapped CS/MISO: CS=RXD(44), MISO=TXD(43), MOSI=SDA(8), SCLK=SCL(9) */
    if (!ok) {
        ok = cc1101_probe_candidate_locked(TPMS_CC1101_MISO_GPIO, TPMS_CC1101_CS_GPIO,
                                            GPIO_NUM_8, GPIO_NUM_9);
    }
    /* 4. Swapped CS/MISO & MOSI/SCLK: CS=RXD(44), MISO=TXD(43), MOSI=SCL(9), SCLK=SDA(8) */
    if (!ok) {
        ok = cc1101_probe_candidate_locked(TPMS_CC1101_MISO_GPIO, TPMS_CC1101_CS_GPIO,
                                            GPIO_NUM_9, GPIO_NUM_8);
    }

    if (!ok) {
        /* Restore default pin assignments */
        s_mosi_gpio = GPIO_NUM_8;
        s_sclk_gpio = GPIO_NUM_9;
        cc1101_configure_uart2_pins(TPMS_CC1101_CS_GPIO, TPMS_CC1101_MISO_GPIO);
    }

    xSemaphoreGive(s_spi_mutex);
    return ok;
}

/* GPIO6 (GDO0) edge interrupt handler — 100% IRAM, runs on Core 1 */
static void IRAM_ATTR cc1101_gdo0_isr_handler(void *arg)
{
    (void)arg;
    s_gdo0_edges++;
    if (!s_enabled_receiver) {
        return;
    }

    const uint32_t now = micros_now();
    const unsigned int duration = (unsigned int)(now - s_last_change_us);
    const int level = gdo0_get_level_fast();

    if (!s_receive_mode) {
        if (s_last_change_us == 0 || duration > PRETRIGGER_WINDOW_US) {
            s_pretrigger_count = 0;
            s_pretrigger_pending_pulse = 0;
        }

        if (!level) {
            /* Falling edge: preceding HIGH interval was a pulse */
            s_pretrigger_pending_pulse =
                (duration > MINIMUM_PULSE_LENGTH_US && duration <= PRETRIGGER_WINDOW_US)
                    ? duration
                    : 0;
        } else if (s_pretrigger_pending_pulse > 0 &&
                   duration > MINIMUM_PULSE_LENGTH_US &&
                   duration <= PRETRIGGER_WINDOW_US) {
            /* Rising edge: complete and store the pulse/gap pair */
            uint8_t head = s_pretrigger_head;
            s_pretrigger_pulse[head] = s_pretrigger_pending_pulse;
            s_pretrigger_gap[head] = duration;
            s_pretrigger_head = (uint8_t)((head + 1U) % PRETRIGGER_PAIRS);
            if (s_pretrigger_count < PRETRIGGER_PAIRS) {
                s_pretrigger_count++;
            }
            s_pretrigger_pending_pulse = 0;
        }

        s_last_change_us = now;
        return;
    }

    if (duration > MINIMUM_PULSE_LENGTH_US && s_current_rssi > s_rssi_threshold) {
        s_gdo0_active_edges++;
        pulse_data_t *train = &s_pulse_trains[s_actual_train];
        int16_t idx = s_nrpulses;
        if (!level) {
            train->pulse[idx] = (int)duration;
        } else {
            if (train->pulse[idx] > 0) {
                train->gap[idx] = (int)duration;
                s_nrpulses = (int16_t)((idx + 1) % PD_MAX_PULSES);
            } else if (idx > 1) {
                train->gap[idx - 1] += (int)duration;
            } else {
                train->gap[idx] = (int)duration;
                s_nrpulses = (int16_t)((idx + 1) % PD_MAX_PULSES);
            }
        }
        s_last_change_us = now;
    }
}

static bool dequeue_pulse_train(pulse_data_t *dst)
{
    if (!dst || !s_pulse_trains) {
        return false;
    }
    bool available = false;
    portENTER_CRITICAL(&s_capture_mux);
    if (s_pulse_trains[s_available_train].num_pulses > 0) {
        uint8_t cur = s_available_train;
        memcpy(dst, &s_pulse_trains[cur], sizeof(pulse_data_t));
        memset(&s_pulse_trains[cur], 0, sizeof(pulse_data_t));
        s_available_train = (uint8_t)((cur + 1U) % RECEIVER_BUFFER_SIZE);
        available = true;
    }
    portEXIT_CRITICAL(&s_capture_mux);
    return available;
}

bool tpms_cc1101_set_modulation(bool ook_mode)
{
    if (s_cc1101_online && s_enabled_receiver && s_ook_modulation == ook_mode) {
        return true;
    }
    s_ook_modulation = ook_mode;
    if (!s_cc1101_online) {
        return false;
    }

    s_enabled_receiver = false;
    s_receive_mode = false;
    s_nrpulses = 0;
    s_pretrigger_count = 0;
    s_pretrigger_pending_pulse = 0;

    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
    if (ook_mode) {
        cc1101_apply_ook_profile_locked();
    } else {
        cc1101_apply_fsk_profile_locked();
    }
    xSemaphoreGive(s_spi_mutex);

    s_last_change_us = micros_now();
    s_enabled_receiver = true;
    return true;
}

static void sanitize_csv_field(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dst_size; ++i) {
        char c = src[i];
        if (c == '\r' || c == '\n') {
            continue;
        }
        if (c == '"') {
            c = '\'';
        }
        dst[j++] = c;
    }
    dst[j] = '\0';
}

/* Called from r_api.c data_acquired_handler whenever a decoder emits a message */
void tpms_rtl433_on_decoded(r_device *r_dev, data_t *data, const char *json_str)
{
    (void)r_dev;
    if (!data) {
        return;
    }

    const char *model = "TPMS";
    const char *msg_type = "";
    char sensor_id[15] = {0};
    char flags_str[12] = {0};
    char code_str[24] = {0};
    uint8_t flags_val = 0;
    bool has_pressure = false;
    float pressure_kpa = 0.0f;
    float temperature_c = 0.0f;

    for (data_t *d = data; d != NULL; d = d->next) {
        if (!d->key) {
            continue;
        }
        if (strcmp(d->key, "model") == 0 && d->type == DATA_STRING && d->value.v_ptr) {
            model = (const char *)d->value.v_ptr;
        } else if (strcmp(d->key, "type") == 0 && d->type == DATA_STRING && d->value.v_ptr) {
            msg_type = (const char *)d->value.v_ptr;
        } else if (strcmp(d->key, "id") == 0) {
            if (d->type == DATA_STRING && d->value.v_ptr) {
                strlcpy(sensor_id, (const char *)d->value.v_ptr, sizeof(sensor_id));
            } else if (d->type == DATA_INT) {
                snprintf(sensor_id, sizeof(sensor_id), "%d", d->value.v_int);
            }
        } else if (strcmp(d->key, "pressure_kPa") == 0 || strcmp(d->key, "pressure_kpa") == 0) {
            if (d->type == DATA_DOUBLE) {
                pressure_kpa = (float)d->value.v_dbl;
                has_pressure = true;
            } else if (d->type == DATA_INT) {
                pressure_kpa = (float)d->value.v_int;
                has_pressure = true;
            }
        } else if (!has_pressure && strcmp(d->key, "pressure_bar") == 0) {
            if (d->type == DATA_DOUBLE) {
                pressure_kpa = (float)(d->value.v_dbl * 100.0);
                has_pressure = true;
            }
        } else if (!has_pressure && strcmp(d->key, "pressure_PSI") == 0) {
            if (d->type == DATA_DOUBLE) {
                pressure_kpa = (float)(d->value.v_dbl * 6.89476);
                has_pressure = true;
            }
        } else if (strcmp(d->key, "temperature_C") == 0) {
            if (d->type == DATA_DOUBLE) {
                temperature_c = (float)d->value.v_dbl;
            } else if (d->type == DATA_INT) {
                temperature_c = (float)d->value.v_int;
            }
        } else if (strcmp(d->key, "flags") == 0) {
            if (d->type == DATA_STRING && d->value.v_ptr) {
                strlcpy(flags_str, (const char *)d->value.v_ptr, sizeof(flags_str));
                flags_val = (uint8_t)strtoul(flags_str, NULL, 16);
            } else if (d->type == DATA_INT) {
                flags_val = (uint8_t)d->value.v_int;
                snprintf(flags_str, sizeof(flags_str), "%02X", (unsigned)flags_val);
            }
        } else if (strcmp(d->key, "code") == 0 && d->type == DATA_STRING && d->value.v_ptr) {
            strlcpy(code_str, (const char *)d->value.v_ptr, sizeof(code_str));
        }
    }

    bool is_tpms = (strcmp(msg_type, "TPMS") == 0);
    if (!is_tpms && !has_pressure) {
        return;
    }

    if (pressure_kpa < 0.0f) {
        pressure_kpa = 0.0f;
    }
    long cbar = lroundf(pressure_kpa);
    if (cbar < 0) cbar = 0;
    if (cbar > 999) cbar = 999;

    /* Prefer Jansite-TY468 if already staged for this pulse train */
    bool keep_existing = s_staged_decode.valid &&
                         (strcmp(s_staged_decode.pkt.model, "Jansite-TY468") == 0) &&
                         (strcmp(model, "Jansite-TY468") != 0);
    if (keep_existing) {
        return;
    }

    memset(&s_staged_decode, 0, sizeof(s_staged_decode));
    s_staged_decode.valid = true;
    s_staged_decode.pkt.magic = TPMS_ESPNOW_MAGIC;
    s_staged_decode.pkt.version = TPMS_ESPNOW_VERSION;
    s_staged_decode.pkt.msg_type = TPMS_MSG_SENSOR_DATA;
    s_staged_decode.pkt.wheel = 0;
    s_staged_decode.pkt.wifi_channel = 0;
    s_staged_decode.pkt.pressure_cbar = (uint16_t)cbar;
    s_staged_decode.pkt.pressure_kpa = pressure_kpa;
    s_staged_decode.pkt.temperature_c = temperature_c;
    s_staged_decode.pkt.flags = flags_val;
    strlcpy(s_staged_decode.pkt.sensor_id, sensor_id, sizeof(s_staged_decode.pkt.sensor_id));
    strlcpy(s_staged_decode.pkt.model, model, sizeof(s_staged_decode.pkt.model));
    sanitize_csv_field(s_staged_decode.flags_str, sizeof(s_staged_decode.flags_str), flags_str);
    sanitize_csv_field(s_staged_decode.code_str, sizeof(s_staged_decode.code_str), code_str);
    sanitize_csv_field(s_staged_decode.raw_json, sizeof(s_staged_decode.raw_json), json_str);
}

static void rtl433_json_cb(char *message)
{
    s_decoded_messages++;
    printf("%" PRIu32 ": Received message : %s\n", s_decoded_messages, message ? message : "{}");
    if (s_auto_scan_modulation) {
        s_auto_scan_modulation = false;
        ESP_LOGI(TAG, "[TPMS] Auto-locked modulation to %s after decoded message!",
                 s_ook_modulation ? "OOK" : "FSK");
    }
}

static bool init_rtl433_tpms_decoders(void)
{
    r_cfg_t *cfg = &s_rtl_cfg;
    if (cfg->demod) {
        return true;
    }

    r_init_cfg(cfg);
    add_log_output(cfg, NULL);
    cfg->conversion_mode = CONVERT_SI;
    cfg->callback = rtl433_json_cb;
    cfg->messageBuffer = s_json_buffer;
    cfg->bufferSize = sizeof(s_json_buffer);

    /* Register only Jansite TY-468 / KKMOON OOK TPMS decoder */
    static const r_device *const k_tpms_devices[] = {
        &tpms_jansite_ty468,
    };

    const size_t num_devs = sizeof(k_tpms_devices) / sizeof(k_tpms_devices[0]);
    for (size_t i = 0; i < num_devs; ++i) {
        r_device tmp = *k_tpms_devices[i];
        tmp.protocol_num = (unsigned)(i + 1U);
        if (tmp.disabled <= 0) {
            register_protocol(cfg, &tmp, NULL);
        }
    }

    ESP_LOGI(TAG, "Registered 1 dedicated OOK TPMS decoder (%s) in rtl_433 engine", tpms_jansite_ty468.name);
    return true;
}

static void cc1101_decoder_task(void *arg)
{
    (void)arg;
    pulse_data_t *pulses = NULL;

    /* Install GPIO ISR service on Core 1 so Core 0's RGB LCD bounce-buffer DMA ISR
     * never blocks or delays 110 us Manchester GDO0 edges. */
    esp_err_t isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    }
    esp_err_t add_err = gpio_isr_handler_add(TPMS_CC1101_GDO0_GPIO, cc1101_gdo0_isr_handler, NULL);
    ESP_LOGI(TAG, "GDO0(GPIO%d) IRAM ISR attached on CPU Core %d (service=%s, handler=%s)",
             (int)TPMS_CC1101_GDO0_GPIO, (int)xPortGetCoreID(),
             esp_err_to_name(isr_err), esp_err_to_name(add_err));

    for (;;) {
        if (xQueueReceive(s_decoder_queue, &pulses, portMAX_DELAY) != pdTRUE || !pulses) {
            continue;
        }

        ESP_LOGI(TAG,
                 "[RF_CAPTURE] #%" PRIu32 " mod=%s dur=%lu us pulses=%u rssi=%d dBm (thresh=%d dBm)",
                 s_total_signals,
                 s_ook_modulation ? "OOK" : "FSK",
                 (unsigned long)pulses->signalDuration,
                 pulses->num_pulses,
                 pulses->signalRssi,
                 s_rssi_threshold);

        pulses->sample_rate = 1.0e6f;
        s_rtl_cfg.demod->pulse_data = *pulses;
        s_staged_decode.valid = false;

        int events = 0;
        if (s_ook_modulation) {
            events = run_ook_demods(&s_rtl_cfg.demod->r_devs, pulses);
            if (events == 0) {
                events = run_fsk_demods(&s_rtl_cfg.demod->r_devs, pulses);
            }
        } else {
            events = run_fsk_demods(&s_rtl_cfg.demod->r_devs, pulses);
            if (events == 0) {
                events = run_ook_demods(&s_rtl_cfg.demod->r_devs, pulses);
            }
        }

        if (s_staged_decode.valid) {
            uint32_t now_ms = (uint32_t)rtl_433_millis();
            uint32_t seq = ++s_decode_seq;

            tpms_espnow_packet_t pkt = s_staged_decode.pkt;
            pkt.rssi_dbm = (int16_t)pulses->signalRssi;
            pkt.seq = seq;
            pkt.uptime_ms = now_ms;

            printf("[DECODED_CSV] %" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%s,%.2f,%d,%lu,%u,%s,%s,%.1f,%.2f,%.1f,%s,%s,\"%s\"\n",
                   seq,
                   seq,
                   now_ms,
                   s_ook_modulation ? "OOK" : "FSK",
                   (double)TPMS_CC1101_FREQ_MHZ,
                   pulses->signalRssi,
                   (unsigned long)pulses->signalDuration,
                   pulses->num_pulses,
                   pkt.model,
                   pkt.sensor_id,
                   (double)pkt.pressure_kpa,
                   (double)pkt.pressure_kpa * 0.01,
                   (double)pkt.temperature_c,
                   s_staged_decode.flags_str,
                   s_staged_decode.code_str,
                   s_staged_decode.raw_json);

            (void)tpms_espnow_ingest_packet(&pkt);
            s_staged_decode.valid = false;
        } else {
            /* Log brief pulse/gap sample for unparsed signals to aid RF diagnostics */
            unsigned sample_n = pulses->num_pulses < 8U ? pulses->num_pulses : 8U;
            char sample_buf[160] = {0};
            size_t pos = 0;
            for (unsigned i = 0; i < sample_n && pos + 20 < sizeof(sample_buf); ++i) {
                pos += (size_t)snprintf(sample_buf + pos, sizeof(sample_buf) - pos,
                                        "%s%d/%d", (i == 0 ? "" : " "),
                                        pulses->pulse[i], pulses->gap[i]);
            }
            ESP_LOGI(TAG, "[RF_UNPARSED] pulses=%u dur=%lu us rssi=%d dBm first_pairs=[%s]",
                     pulses->num_pulses, (unsigned long)pulses->signalDuration,
                     pulses->signalRssi, sample_buf);
        }

        free(pulses);
    }
}

static void poll_serial_commands(uint32_t *last_scan_switch_ms)
{
    int ch = getchar();
    while (ch != EOF) {
        if (ch == 'o' || ch == 'O') {
            s_auto_scan_modulation = false;
            tpms_cc1101_set_modulation(true);
            ESP_LOGI(TAG, "[Scan] Locked modulation to OOK (433.92 MHz, 812.5 kHz BW)");
        } else if (ch == 'f' || ch == 'F') {
            s_auto_scan_modulation = false;
            tpms_cc1101_set_modulation(false);
            ESP_LOGI(TAG, "[Scan] Locked modulation to FSK (433.92 MHz, 20 kbps, 325 kHz BW)");
        } else if (ch == 'a' || ch == 'A') {
            s_auto_scan_modulation = true;
            *last_scan_switch_ms = (uint32_t)rtl_433_millis();
            ESP_LOGI(TAG, "[Scan] Enabled Dual FSK/OOK Auto-Scan (%u ms interval)",
                     (unsigned)MODULATION_SCAN_INTERVAL_MS);
        } else if (ch == 's' || ch == 'S') {
            if (s_cc1101_online) {
                xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
                cc1101_log_status_locked();
                xSemaphoreGive(s_spi_mutex);
            } else {
                ESP_LOGW(TAG,
                         "CC1101 not detected yet (UART2: CSN=GPIO%d/TXD, MISO=GPIO%d/RXD [check SW1=UART2]; I2C: MOSI=GPIO%d/SDA, SCLK=GPIO%d/SCL; SensorAD: GDO0=GPIO%d/AD)",
                         (int)s_cs_gpio, (int)s_miso_gpio,
                         (int)TPMS_CC1101_MOSI_GPIO, (int)TPMS_CC1101_SCLK_GPIO,
                         (int)TPMS_CC1101_GDO0_GPIO);
            }
        }
        ch = getchar();
    }
}

static void cc1101_rx_task(void *arg)
{
    (void)arg;
    uint32_t last_probe_log_ms = 0;
    uint32_t last_deaf_reset_ms = (uint32_t)rtl_433_millis();
    uint32_t last_scan_switch_ms = last_deaf_reset_ms;
    uint32_t last_rf_status_ms = last_deaf_reset_ms;

    for (;;) {
        uint32_t now_ms = (uint32_t)rtl_433_millis();
        poll_serial_commands(&last_scan_switch_ms);

        /* 1. Probe and auto-initialize CC1101 (supports hot-plugging after boot) */
        if (!s_cc1101_online) {
            if (cc1101_probe_and_configure()) {
                s_rssi_calibrated = false;
                s_total_rssi = 0;
                s_rssi_count = 0;
                s_peak_rssi = -256;
                s_above_threshold = 0;
                s_receive_mode = false;
                s_nrpulses = 0;
                s_last_change_us = micros_now();
                s_cc1101_online = true;
                s_enabled_receiver = true;
                last_rf_status_ms = now_ms;
                ESP_LOGI(TAG,
                         "[Mode] Locked to %s (%.2f MHz). Serial commands: 'o'=OOK, 'f'=FSK, 'a'=Auto-Scan, 's'=Status",
                         s_ook_modulation ? "OOK" : "FSK", (double)TPMS_CC1101_FREQ_MHZ);
            } else {
                if (last_probe_log_ms == 0 || (now_ms - last_probe_log_ms) >= 10000U) {
                    last_probe_log_ms = now_ms;
                    ESP_LOGW(TAG,
                             "Waiting for CC1101 on UART2+SensorAD+I2C (CSN=GPIO%d/TXD, MISO=GPIO%d/RXD [SW1=UART2], MOSI=GPIO%d/SDA, SCLK=GPIO%d/SCL, GDO0=GPIO%d/AD)...",
                             (int)TPMS_CC1101_CS_GPIO, (int)TPMS_CC1101_MISO_GPIO,
                             (int)TPMS_CC1101_MOSI_GPIO, (int)TPMS_CC1101_SCLK_GPIO,
                             (int)TPMS_CC1101_GDO0_GPIO);
                }
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }

        /* 2. Hourly CC1101 deaf workaround + health check */
        if ((now_ms - last_deaf_reset_ms) >= 3600000U) {
            last_deaf_reset_ms = now_ms;
            xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
            uint8_t ver = cc1101_spi_read_reg(CC1101_REG_VERSION);
            if (ver == 0x00 || ver == 0xFF) {
                s_cc1101_online = false;
                s_enabled_receiver = false;
                xSemaphoreGive(s_spi_mutex);
                continue;
            }
            cc1101_spi_strobe(CC1101_CMD_IDLE);
            cc1101_spi_strobe(CC1101_CMD_RX);
            xSemaphoreGive(s_spi_mutex);
        }

        /* 3. Optional OOK/FSK auto-scan when idle */
        if (s_auto_scan_modulation && !s_receive_mode &&
            (now_ms - last_scan_switch_ms) >= MODULATION_SCAN_INTERVAL_MS) {
            last_scan_switch_ms = now_ms;
            tpms_cc1101_set_modulation(!s_ook_modulation);
        }

        /* 4. Poll CC1101 RSSI and update adaptive threshold */
        if (s_enabled_receiver) {
            xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
            int rssi = cc1101_read_rssi_dbm();
            xSemaphoreGive(s_spi_mutex);

            s_current_rssi = rssi;
            s_rssi_count++;
            s_total_rssi += rssi;
            if (rssi > s_peak_rssi) {
                s_peak_rssi = rssi;
            }
            if (rssi > s_rssi_threshold) {
                s_above_threshold++;
            }

            if (s_rssi_calibrated && (now_ms - last_rf_status_ms) >= 5000U) {
                last_rf_status_ms = now_ms;
                ESP_LOGI(TAG,
                         "[RF_STATUS] mod=%s rssi=%d avg=%d thresh=%d peak=%d gdo0_lvl=%d edges=%" PRIu32 " act_edges=%" PRIu32 " sigs=%" PRIu32 " ign=%" PRIu32 " dec=%" PRIu32,
                         s_ook_modulation ? "OOK" : "FSK",
                         rssi, s_average_rssi, s_rssi_threshold, s_peak_rssi,
                         gdo0_get_level_fast(),
                         s_gdo0_edges, s_gdo0_active_edges,
                         s_total_signals, s_ignored_signals, s_decoded_messages);
            }

            int target_samples = s_rssi_calibrated ? RSSI_PERIODIC_SAMPLES : RSSI_INITIAL_SAMPLES;
            if (s_rssi_count >= target_samples) {
                s_average_rssi = s_total_rssi / s_rssi_count;
                s_rssi_threshold = s_average_rssi + RSSI_THRESHOLD_DELTA_DB;
                if (!s_rssi_calibrated) {
                    s_rssi_calibrated = true;
                    ESP_LOGI(TAG,
                              "Initial RSSI calibration complete: average %d dBm, threshold %d dBm, samples %d, peak %d dBm, gdo0_edges=%" PRIu32 "; reception enabled",
                             s_average_rssi, s_rssi_threshold, target_samples, s_peak_rssi, s_gdo0_edges);
                } else {
                    ESP_LOGD(TAG,
                             "Average RSSI %d dBm, adjusted threshold %d dBm, peak %d dBm, above %d",
                             s_average_rssi, s_rssi_threshold, s_peak_rssi, s_above_threshold);
                }
                s_peak_rssi = -256;
                s_above_threshold = 0;
                s_total_rssi = 0;
                s_rssi_count = 0;
            }

            if (!s_rssi_calibrated) {
                vTaskDelay(1);
                continue;
            }

            uint32_t now_us = micros_now();
            if (rssi > s_rssi_threshold) {
                if (!s_receive_mode) {
                    s_signal_start_us = now_us;
                    s_signal_rssi = rssi;

                    /* Prepend recent pretrigger edges captured before RSSI gate opened */
                    portENTER_CRITICAL(&s_capture_mux);
                    uint8_t selected = 0;
                    unsigned int pretrigger_dur = 0;
                    for (uint8_t i = 0; i < s_pretrigger_count; ++i) {
                        uint8_t idx = (uint8_t)((s_pretrigger_head + PRETRIGGER_PAIRS - 1U - i) % PRETRIGGER_PAIRS);
                        unsigned int pair_dur = s_pretrigger_pulse[idx] + s_pretrigger_gap[idx];
                        if (pretrigger_dur + pair_dur > PRETRIGGER_WINDOW_US) {
                            break;
                        }
                        pretrigger_dur += pair_dur;
                        selected++;
                    }

                    uint8_t first = (uint8_t)((s_pretrigger_head + PRETRIGGER_PAIRS - selected) % PRETRIGGER_PAIRS);
                    pulse_data_t *train = &s_pulse_trains[s_actual_train];
                    for (uint8_t i = 0; i < selected; ++i) {
                        uint8_t idx = (uint8_t)((first + i) % PRETRIGGER_PAIRS);
                        train->pulse[i] = (int)s_pretrigger_pulse[idx];
                        train->gap[i] = (int)s_pretrigger_gap[idx];
                    }
                    s_nrpulses = selected;

                    if (gdo0_get_level_fast() == 0 &&
                        s_pretrigger_pending_pulse > 0 &&
                        s_nrpulses < PD_MAX_PULSES) {
                        train->pulse[s_nrpulses] = (int)s_pretrigger_pending_pulse;
                        pretrigger_dur += s_pretrigger_pending_pulse;
                    }

                    s_pretrigger_count = 0;
                    s_pretrigger_pending_pulse = 0;
                    s_receive_mode = true;
                    portEXIT_CRITICAL(&s_capture_mux);

                    s_signal_start_us -= pretrigger_dur;
                } else if (rssi > s_signal_rssi) {
                    s_signal_rssi = rssi;
                }
                s_signal_end_us = now_us;
            } else if (s_receive_mode &&
                       (uint32_t)(now_us - s_signal_end_us) <
                           (s_ook_modulation ? OOK_INTERFRAME_GAP_US : FSK_INTERFRAME_GAP_US) &&
                       s_nrpulses > PD_MIN_PULSES) {
                /* Bridge inter-frame gaps within a multi-frame TPMS burst */
            } else if (s_receive_mode) {
                s_receive_mode = false;
                s_total_signals++;
                uint32_t sig_dur = s_signal_end_us - s_signal_start_us;
                uint32_t min_sig_len = s_ook_modulation ? OOK_MIN_SIGNAL_LENGTH_US : FSK_MIN_SIGNAL_LENGTH_US;

                if (s_nrpulses > PD_MIN_PULSES && sig_dur >= min_sig_len) {
                    uint8_t next_train = (uint8_t)((s_actual_train + 1U) % RECEIVER_BUFFER_SIZE);
                    portENTER_CRITICAL(&s_capture_mux);
                    if (s_pulse_trains[next_train].num_pulses == 0) {
                        pulse_data_t *train = &s_pulse_trains[s_actual_train];
                        int16_t end_idx = s_nrpulses;
                        if (end_idx < PD_MAX_PULSES) {
                            train->gap[end_idx] = (int)(micros_now() - s_last_change_us);
                            train->num_pulses = (unsigned)(end_idx + 1);
                        } else {
                            train->num_pulses = PD_MAX_PULSES;
                        }
                        train->signalDuration = sig_dur;
                        train->signalRssi = s_signal_rssi;
                        train->centerfreq_hz = (uint32_t)(TPMS_CC1101_FREQ_MHZ * 1.0e6f);
                        s_actual_train = next_train;
                    } else {
                        memset(&s_pulse_trains[s_actual_train], 0, sizeof(pulse_data_t));
                    }
                    portEXIT_CRITICAL(&s_capture_mux);
                } else {
                    s_ignored_signals++;
                    if (s_nrpulses >= 8) {
                        ESP_LOGI(TAG, "[RF_IGNORED] dur=%lu us pulses=%d rssi=%d dBm (min_pulses=%d min_dur=%lu)",
                                 (unsigned long)sig_dur, (int)s_nrpulses, s_signal_rssi,
                                 (int)PD_MIN_PULSES, (unsigned long)min_sig_len);
                    }
                }
                s_nrpulses = 0;
            }

            /* 5. Drain completed pulse trains into the decoder task queue */
            if (s_pulse_trains[s_available_train].num_pulses > 0) {
                pulse_data_t *copy = (pulse_data_t *)heap_caps_calloc(
                    1, sizeof(pulse_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (copy) {
                    if (dequeue_pulse_train(copy)) {
                        if (copy->num_pulses > PD_MIN_PULSES) {
                            if (xQueueSend(s_decoder_queue, &copy, 0) != pdTRUE) {
                                free(copy);
                            }
                        } else {
                            free(copy);
                        }
                    } else {
                        free(copy);
                    }
                }
            }
        }

        vTaskDelay(1);
    }
}

esp_err_t tpms_cc1101_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_spi_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_spi_mutex != NULL, ESP_ERR_NO_MEM, TAG, "SPI mutex alloc failed");

    s_pulse_trains = (pulse_data_t *)heap_caps_calloc(
        RECEIVER_BUFFER_SIZE, sizeof(pulse_data_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_pulse_trains != NULL, ESP_ERR_NO_MEM, TAG, "Pulse train buffer alloc failed");

    s_decoder_queue = xQueueCreate(5, sizeof(pulse_data_t *));
    ESP_RETURN_ON_FALSE(s_decoder_queue != NULL, ESP_ERR_NO_MEM, TAG, "Decoder queue alloc failed");

    ESP_RETURN_ON_FALSE(init_rtl433_tpms_decoders(), ESP_ERR_NO_MEM, TAG, "rtl_433 init failed");

    /* Configure UART2 connector pins: CSN=GPIO43 (TXD), MISO=GPIO44 (RXD) */
    cc1101_configure_uart2_pins(TPMS_CC1101_CS_GPIO, TPMS_CC1101_MISO_GPIO);

    /* Configure GDO0 (GPIO6 on Sensor AD connector J8) with ANYEDGE interrupt */
    gpio_config_t gdo0_cfg = {
        .pin_bit_mask = 1ULL << TPMS_CC1101_GDO0_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&gdo0_cfg), TAG, "GDO0 GPIO config failed");

    s_initialized = true;

    /* Create decoder and rx tasks on Core 1 so Core 0 is 100% dedicated to LVGL & RGB LCD DMA */
    xTaskCreatePinnedToCore(cc1101_decoder_task, "rtl433_dec", 20480, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(cc1101_rx_task, "cc1101_rx", 4096, NULL, 3, NULL, 1);

    ESP_LOGI(TAG,
             "Direct CC1101 OOK_Receiver initialized on UART2+SensorAD+I2C (CSN=GPIO%d/TXD, MISO=GPIO%d/RXD, MOSI=GPIO%d/SDA, SCLK=GPIO%d/SCL, GDO0=GPIO%d/AD)",
             (int)TPMS_CC1101_CS_GPIO, (int)TPMS_CC1101_MISO_GPIO,
             (int)TPMS_CC1101_MOSI_GPIO, (int)TPMS_CC1101_SCLK_GPIO,
             (int)TPMS_CC1101_GDO0_GPIO);
    return ESP_OK;
}

bool tpms_cc1101_is_online(void)
{
    return s_cc1101_online;
}
