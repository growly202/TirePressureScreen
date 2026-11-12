#ifndef TPMS_CC1101_H
#define TPMS_CC1101_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

/* Default CC1101 wiring on Waveshare ESP32-S3-Touch-LCD-7 using onboard PH2.0 connectors
 * (UART2 + Sensor AD + I2C — set onboard slide switch SW1 to "UART2"):
 *   VCC (3.3V)  -> Sensor AD (J8 Pin 1: 3V3)  [or UART2 H3 Pin 1: 3V3 / I2C H1 Pin 1: VCC]
 *   GND         -> Sensor AD (J8 Pin 2: GND)  [or UART2 H3 Pin 2: GND / I2C H1 Pin 2: GND]
 *   GDO0        -> GPIO6  (Sensor AD connector J8 Pin 3: AD)
 *   CSN         -> GPIO43 (UART2 connector H3 Pin 4: TXD)  [auto-detects if swapped with RXD]
 *   MISO (SO)   -> GPIO44 (UART2 connector H3 Pin 3: RXD)  [auto-detects if swapped with TXD]
 *   MOSI (SI)   -> GPIO8  (I2C connector H1 Pin 3: SDA)
 *   SCLK (SCK)  -> GPIO9  (I2C connector H1 Pin 4: SCL)
 */
#ifndef TPMS_CC1101_MOSI_GPIO
#define TPMS_CC1101_MOSI_GPIO GPIO_NUM_8
#endif

#ifndef TPMS_CC1101_SCLK_GPIO
#define TPMS_CC1101_SCLK_GPIO GPIO_NUM_9
#endif

#ifndef TPMS_CC1101_MISO_GPIO
#define TPMS_CC1101_MISO_GPIO GPIO_NUM_44
#endif

#ifndef TPMS_CC1101_CS_GPIO
#define TPMS_CC1101_CS_GPIO GPIO_NUM_43
#endif

#ifndef TPMS_CC1101_GDO0_GPIO
#define TPMS_CC1101_GDO0_GPIO GPIO_NUM_6
#endif

#ifndef TPMS_CC1101_FREQ_MHZ
#define TPMS_CC1101_FREQ_MHZ 433.92f
#endif

/* Initialize UART2/SensorAD/I2C CC1101 interface, rtl_433 TPMS decoders, and RX tasks. */
esp_err_t tpms_cc1101_init(void);

/* Return true once the CC1101 transceiver has been detected over SPI. */
bool tpms_cc1101_is_online(void);

/* Switch CC1101 between OOK (true) and FSK (false) 433.92 MHz modulation. */
bool tpms_cc1101_set_modulation(bool ook_mode);

#endif /* TPMS_CC1101_H */
