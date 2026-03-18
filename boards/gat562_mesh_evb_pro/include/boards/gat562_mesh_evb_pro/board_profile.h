#pragma once

#include <cstdint>

namespace boards::gat562_mesh_evb_pro
{

struct BoardProfile
{
    struct LedPins
    {
        int status = -1;
        int notification = -1;
        bool active_high = true;
        bool notification_shares_status = false;
    };

    struct InputPins
    {
        int button_primary = -1;
        int button_secondary = -1;
        int joystick_up = -1;
        int joystick_down = -1;
        int joystick_left = -1;
        int joystick_right = -1;
        int joystick_press = -1;
        bool buttons_need_pullup = true;
        bool joystick_need_pullup = true;
        bool joystick_is_two_way = false;
        uint16_t debounce_ms = 50;
    };

    struct I2cPins
    {
        int sda = -1;
        int scl = -1;
        uint8_t address = 0x3C;
    };

    struct UartPins
    {
        int rx = -1;
        int tx = -1;
        int aux = -1;
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
        int power_en = -1;
        bool dio2_controls_rf_switch = true;
        float dio3_tcxo_voltage = 1.8f;
    };

    struct GpsProfile
    {
        UartPins uart{};
        int pps = -1;
        uint32_t baud_rate = 9600;
    };

    struct BatteryProfile
    {
        int adc_pin = -1;
        uint8_t adc_resolution_bits = 12;
        float aref_voltage = 3.0f;
        float adc_multiplier = 1.73f;
    };

    struct ProductBoundary
    {
        bool supports_meshtastic = true;
        bool supports_meshcore = true;
        bool supports_ble = true;
        bool supports_lora = true;
        bool supports_gnss = true;
        bool supports_team = false;
        bool supports_hostlink = false;
        bool supports_sdcard = false;
        bool supports_cjk_input = false;
        bool supports_pinyin_ime = false;
        bool supports_touch = false;
        bool supports_keyboard = false;
    };

    struct ProductIdentity
    {
        const char* long_name = "GAT562";
        const char* short_name = "GAT562";
        const char* ble_name = "GAT562";
    };

    LedPins leds{};
    InputPins inputs{};
    I2cPins oled_i2c{};
    UartPins jlink_cdc{};
    LoraPins lora{};
    GpsProfile gps{};
    BatteryProfile battery{};
    int peripheral_3v3_enable = -1;
    bool has_screen = true;
    bool use_ssd1306 = true;
    uint32_t max_flash_size = 815104;
    uint32_t max_ram_size = 248832;
    uint32_t bootloader_settings_addr = 0xFF000;
    ProductIdentity identity{};
    ProductBoundary boundary{};
};

inline constexpr BoardProfile kBoardProfile{
    {35, 36, true, false},
    {9, 12, 28, 4, 30, 31, 10, true, true, false, 50},
    {13, 14, 0x3C},
    {8, 6, -1},
    {{43, 45, 44, 42}, 47, 46, 38, 37, true, 1.8f},
    {{15, 16, -1}, 17, 9600},
    {5, 12, 3.0f, 1.73f},
    34,
    true,
    true,
    815104,
    248832,
    0xFF000,
    {"GAT562", "GAT562", "GAT562"},
    {}
};

} // namespace boards::gat562_mesh_evb_pro
