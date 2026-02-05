#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303A
#define USB_PID 0x1001
#define USB_MANUFACTURER "LILYGO"
#define USB_PRODUCT "TRAIL MATE"

// Basic UART
static const uint8_t TX = 43;
static const uint8_t RX = 44;

// I2C
static const uint8_t SDA = 18;
static const uint8_t SCL = 8;
// Keep sensor/PMU on the primary I2C bus unless a verified alt bus is known.
// 39/40 conflicts with SD_CS/SPI_SCK on T-Deck and can break SD init.
#define SENSOR_SDA (SDA)
#define SENSOR_SCL (SCL)

// SPI (T-Deck defaults)
static const uint8_t SS = 9;
static const uint8_t MOSI = 41;
static const uint8_t MISO = 38;
static const uint8_t SCK = 40;

// Board power enable (required very early on T-Deck)
#define BOARD_POWERON (10)

// Display pins (aligned with LilyGo T-Deck examples)
#define DISP_MOSI (MOSI)
#define DISP_MISO (MISO)
#define DISP_SCK (SCK)
#define DISP_RST (-1)
#define DISP_CS (12)
#define DISP_DC (11)
#define DISP_BL (42)

// LoRa pins (aligned with LilyGo T-Deck examples)
#define LORA_SCK (SCK)
#define LORA_MISO (MISO)
#define LORA_MOSI (MOSI)
#define LORA_CS (9)
#define LORA_RST (17)
#define LORA_BUSY (13)
#define LORA_IRQ (45)

// GPS pins (T-Deck examples show 43/44; keep explicit)
#define GPS_TX (43)
#define GPS_RX (44)
#define GPS_PPS (-1)

// Keyboard / inputs (disabled placeholders)
#define KB_INT (-1)
#define KB_BACKLIGHT (-1)

// Trackball / joystick-like inputs (from LilyGo T-Deck examples)
#define TRACKBALL_UP (3)
#define TRACKBALL_DOWN (15)
#define TRACKBALL_LEFT (1)
#define TRACKBALL_RIGHT (2)
#define TRACKBALL_CLICK (0)

// Interrupt pins (not present / unknown)
#define RTC_INT (-1)
#define NFC_INT (-1)
#define SENSOR_INT (-1)
#define NFC_CS (-1)

// Power key
#define POWER_KEY (0)
#define BOOT_KEY (0)

// SD
#define SD_CS (39)
// Battery ADC (per LilyGo utilities.h)
#define BOARD_BAT_ADC (4)

// Feature flags: keep minimal; add only when verified on T-Deck
// #define USING_ST25R3916
// #define USING_BHI260_SENSOR
// Enable encoder-style input; T-Deck trackball will be mapped to rotary events.
#define USING_INPUT_DEV_ROTARY
// #define USING_INPUT_DEV_KEYBOARD
// #define HAS_SD_CARD_SOCKET

#endif /* Pins_Arduino_h */
