#ifndef _VARIANT_GAT562_MESH_EVB_PRO_
#define _VARIANT_GAT562_MESH_EVB_PRO_

#define RAK4630
#define VARIANT_MCK (64000000ul)
#define USE_LFXO

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

#define PIN_LED1 (35)
#define LED_BUILTIN PIN_LED1
#define LED_BLUE (36)
#define LED_GREEN PIN_LED1
#define LED_NOTIFICATION LED_BLUE
#define LED_STATE_ON 1

#define PIN_BUTTON1 (9)
#define BUTTON_NEED_PULLUP
#define PIN_BUTTON2 (12)

#define PIN_A0 (5)
#define PIN_A1 (31)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;

#define ADC_RESOLUTION 14
#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define PIN_NFC2 (10)
static const uint8_t AREF = PIN_AREF;

#define PIN_SERIAL1_RX (15)
#define PIN_SERIAL1_TX (16)
#define PIN_SERIAL2_RX (8)
#define PIN_SERIAL2_TX (6)

#define SPI_INTERFACES_COUNT 2
#define PIN_SPI_MISO (45)
#define PIN_SPI_MOSI (44)
#define PIN_SPI_SCK (43)
#define PIN_SPI1_MISO (29)
#define PIN_SPI1_MOSI (30)
#define PIN_SPI1_SCK (3)

static const uint8_t SS = 42;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

#define HAS_SCREEN 1
#define USE_SSD1306

#define WIRE_INTERFACES_COUNT 1
#define PIN_WIRE_SDA (13)
#define PIN_WIRE_SCL (14)

#define PIN_QSPI_SCK 3
#define PIN_QSPI_CS 26
#define PIN_QSPI_IO0 30
#define PIN_QSPI_IO1 29
#define PIN_QSPI_IO2 28
#define PIN_QSPI_IO3 2

#define EXTERNAL_FLASH_DEVICES IS25LP080D
#define EXTERNAL_FLASH_USE_QSPI

#define USE_SX1262
#define SX126X_CS (42)
#define SX126X_DIO1 (47)
#define SX126X_BUSY (46)
#define SX126X_RESET (38)
#define SX126X_POWER_EN (37)
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define NRF_APM
#define PIN_3V3_EN (34)

#define PIN_GPS_PPS (17)
#define GPS_BAUDRATE 9600
#define GPS_RX_PIN PIN_SERIAL1_RX
#define GPS_TX_PIN PIN_SERIAL1_TX

#define BATTERY_PIN PIN_A0
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER 1.73

#ifdef __cplusplus
}
#endif

#endif
