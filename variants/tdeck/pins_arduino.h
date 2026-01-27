#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID          0x303A
#define USB_PID          0x1001
#define USB_MANUFACTURER "LILYGO"
#define USB_PRODUCT      "T-Deck"

// Basic UART
static const uint8_t TX = 43;
static const uint8_t RX = 44;

// I2C
static const uint8_t SDA = 18;
static const uint8_t SCL = 8;

// SPI (T-Deck defaults)
static const uint8_t SS   = 9;
static const uint8_t MOSI = 41;
static const uint8_t MISO = 38;
static const uint8_t SCK  = 40;

// Display pins (placeholder mapping for compile)
#define DISP_MOSI (MOSI)
#define DISP_MISO (MISO)
#define DISP_SCK  (SCK)
#define DISP_RST  (-1)
#define DISP_CS   (SS)
#define DISP_DC   (7)
#define DISP_BL   (42)

// LoRa pins (placeholder mapping for compile)
#define LORA_SCK  (SCK)
#define LORA_MISO (MISO)
#define LORA_MOSI (MOSI)
#define LORA_CS   (SS)
#define LORA_RST  (6)
#define LORA_BUSY (5)
#define LORA_IRQ  (4)

// GPS pins (T-Deck examples show 43/44; keep explicit)
#define GPS_TX  (43)
#define GPS_RX  (44)
#define GPS_PPS (-1)

// Keyboard / inputs (disabled placeholders)
#define KB_INT       (-1)
#define KB_BACKLIGHT (-1)

// Rotary (not present on T-Deck)
#define ROTARY_A (-1)
#define ROTARY_B (-1)
#define ROTARY_C (-1)

// Interrupt pins (not present / unknown)
#define RTC_INT    (-1)
#define NFC_INT    (-1)
#define SENSOR_INT (-1)
#define NFC_CS     (-1)

// Power key
#define POWER_KEY (0)
#define BOOT_KEY  (0)

// SD
#define SD_CS (SS)

// Feature flags: keep minimal; add only when verified on T-Deck
// #define USING_ST25R3916
// #define USING_BHI260_SENSOR
// #define USING_INPUT_DEV_ROTARY
// #define USING_INPUT_DEV_KEYBOARD
// #define HAS_SD_CARD_SOCKET

#endif /* Pins_Arduino_h */
