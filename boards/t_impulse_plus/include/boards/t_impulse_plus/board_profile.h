#pragma once

#include <cstdint>

namespace boards::t_impulse_plus
{

#ifndef _PINNUM
#define _PINNUM(port, pin) ((port)*32 + (pin))
#endif

struct BoardProfile
{
    struct LedPins
    {
        int status = -1;
        bool active_high = true;
    };

    struct InputPins
    {
        int function_touch = -1;
        bool active_high = true;
        bool use_pullup = false;
        uint16_t debounce_ms = 45;
        uint32_t long_press_ms = 900;
    };

    struct I2cPins
    {
        int sda = -1;
        int scl = -1;
        uint8_t address = 0x3C;
    };

    struct DisplayProfile
    {
        I2cPins i2c{};
        int reset = -1;
        int physical_width = 128;
        int physical_height = 64;
        int logical_width = 64;
        int logical_height = 32;
        int visible_offset_x = 32;
        int visible_offset_y = 32;
    };

    struct UartPins
    {
        int rx = -1;
        int tx = -1;
        int enable = -1;
    };

    struct SpiPins
    {
        int sck = -1;
        int miso = -1;
        int mosi = -1;
        int cs = -1;
    };

    struct LoraPins
    {
        SpiPins spi{};
        int dio1 = -1;
        int busy = -1;
        int reset = -1;
        int rf_vc1 = -1;
        int rf_vc2 = -1;
        float tcxo_voltage = 1.8f;
    };

    struct GpsProfile
    {
        UartPins uart{};
        uint32_t baud_rate = 38400;
    };

    struct BatteryProfile
    {
        int adc_pin = -1;
        int measurement_enable = -1;
        bool measurement_enable_active_high = true;
        uint8_t adc_resolution_bits = 12;
        float aref_voltage = 3.0f;
        float adc_multiplier = 2.0f;
    };

    struct ProductBoundary
    {
        bool supports_meshtastic = true;
        bool supports_meshcore = true;
        bool supports_ble = true;
        bool supports_lora = true;
        bool supports_gnss = true;
        bool supports_imu = false;
        bool supports_team = false;
        bool supports_hostlink = false;
        bool supports_sdcard = false;
        bool supports_cjk_input = false;
        bool supports_pinyin_ime = false;
        bool supports_touch = true;
        bool supports_keyboard = false;
    };

    struct ProductIdentity
    {
        const char* long_name = "T-Impulse Plus";
        const char* short_name = "TIP";
        const char* ble_name = "T-Impulse";
    };

    LedPins leds{};
    InputPins input{};
    DisplayProfile display{};
    LoraPins lora{};
    GpsProfile gps{};
    BatteryProfile battery{};
    int peripheral_3v3_enable = -1;
    int vibration_motor = -1;
    uint32_t max_flash_size = 815104;
    uint32_t max_ram_size = 248832;
    uint32_t bootloader_settings_addr = 0xFF000;
    ProductIdentity identity{};
    ProductBoundary boundary{};
};

inline constexpr BoardProfile kBoardProfile{
    {_PINNUM(0, 17), false},
    {_PINNUM(1, 4), true, false, 45, 900},
    {{_PINNUM(0, 20), _PINNUM(0, 15), 0x3C}, -1, 128, 64, 64, 32, 32, 32},
    {{_PINNUM(0, 3), _PINNUM(0, 30), _PINNUM(0, 28), _PINNUM(1, 14)},
     _PINNUM(0, 29),
     _PINNUM(0, 31),
     _PINNUM(0, 2),
     _PINNUM(1, 13),
     _PINNUM(1, 7),
     1.8f},
    {{_PINNUM(1, 12), _PINNUM(1, 11), _PINNUM(1, 10)}, 38400},
    {_PINNUM(0, 5), _PINNUM(0, 25), true, 12, 3.0f, 2.0f},
    _PINNUM(0, 14),
    _PINNUM(0, 22),
    815104,
    248832,
    0xFF000,
    {"T-Impulse Plus", "TIP", "T-Impulse"},
    {}};

} // namespace boards::t_impulse_plus
