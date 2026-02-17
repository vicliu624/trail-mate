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

// I2C (main bus)
static const uint8_t SDA = 10;
static const uint8_t SCL = 11;
#define SENSOR_SDA (SDA)
#define SENSOR_SCL (SCL)

// SPI (display bus)
static const uint8_t MOSI = 13;
static const uint8_t MISO = 37; // Unused MISO; keep off LoRa pins
static const uint8_t SCK = 18;
static const uint8_t SS = 12;

// Touch I2C (FT6336U on Wire1)
#define TOUCH_SDA (39)
#define TOUCH_SCL (40)
#define TOUCH_INT (16)
#define TOUCH_RST (-1)

// Interrupts
#define RTC_INT (17)
#define PMU_INT (21)
#define SENSOR_INT (14)

// Display pins (ST7789V3)
#define DISP_MOSI (MOSI)
#define DISP_MISO (MISO)
#define DISP_SCK (SCK)
#define DISP_CS (12)
#define DISP_DC (38)
#define DISP_RST (-1)
#define DISP_BL (45)

// LoRa pins (SX1262)
#define LORA_SCK (3)
#define LORA_MISO (4)
#define LORA_MOSI (1)
#define LORA_CS (5)
#define LORA_RST (8)
#define LORA_BUSY (7)
#define LORA_IRQ (9)

// Audio / mic (not used yet)
#define PDM_SCK (44)
#define PDM_DATA (47)
#define PCM_BCLK (48)
#define PCM_WCLK (15)
#define PCM_DOUT (46)

#endif /* Pins_Arduino_h */
