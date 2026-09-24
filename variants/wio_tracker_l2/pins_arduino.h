#pragma once

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// WIO's user LED is on the I/O expander (P12), not a CPU GPIO or NeoPixel.
// Do not define LED_BUILTIN/RGB_BUILTIN: the generic ESP32-S3 variant's
// GPIO48 NeoPixel alias is incorrect here (GPIO48 is the I2C clock), and
// causes digitalWrite() to retain the otherwise unused RGB/RMT drivers.

// Defaults mirror boards/wio_tracker_l2/board_profile.h. Keep this header
// usable by Arduino's C GPIO implementation as well as C++ library code.
static const uint8_t TX = 17;
static const uint8_t RX = 18;
static const uint8_t SDA = 47;
static const uint8_t SCL = 48;
static const uint8_t SS = 21;
static const uint8_t MOSI = 6;
static const uint8_t MISO = 5;
static const uint8_t SCK = 4;

// ESP32-S3 analog/touch aliases retained from the generic variant. Actual
// board peripheral ownership still comes from the board profile.
static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 5;
static const uint8_t A5 = 6;
static const uint8_t A6 = 7;
static const uint8_t A7 = 8;
static const uint8_t A8 = 9;
static const uint8_t A9 = 10;
static const uint8_t A10 = 11;
static const uint8_t A11 = 12;
static const uint8_t A12 = 13;
static const uint8_t A13 = 14;
static const uint8_t A14 = 15;
static const uint8_t A15 = 16;
static const uint8_t A16 = 17;
static const uint8_t A17 = 18;
static const uint8_t A18 = 19;
static const uint8_t A19 = 20;
static const uint8_t T1 = 1;
static const uint8_t T2 = 2;
static const uint8_t T3 = 3;
static const uint8_t T4 = 4;
static const uint8_t T5 = 5;
static const uint8_t T6 = 6;
static const uint8_t T7 = 7;
static const uint8_t T8 = 8;
static const uint8_t T9 = 9;
static const uint8_t T10 = 10;
static const uint8_t T11 = 11;
static const uint8_t T12 = 12;
static const uint8_t T13 = 13;
static const uint8_t T14 = 14;
