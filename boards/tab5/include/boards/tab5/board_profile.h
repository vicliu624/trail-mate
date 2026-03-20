#pragma once

#include <cstdint>

namespace boards::tab5
{

struct BoardProfile
{
    struct I2cBus
    {
        int port = -1;
        int sda = -1;
        int scl = -1;
    };

    struct UartPort
    {
        int port = -1;
        int tx = -1;
        int rx = -1;
        int aux = -1;
        uint32_t baud_rate = 0;
    };

    struct SdmmcPins
    {
        int d0 = -1;
        int d1 = -1;
        int d2 = -1;
        int d3 = -1;
        int cmd = -1;
        int clk = -1;
    };

    struct AudioI2sPins
    {
        int bclk = -1;
        int mclk = -1;
        int ws = -1;
        int dout = -1;
        int din = -1;
    };

    struct LoRaModulePins
    {
        struct SpiPins
        {
            int host = -1;
            int sck = -1;
            int miso = -1;
            int mosi = -1;
        };

        SpiPins spi{};
        int nss = -1;
        int rst = -1;
        int irq = -1;
        int busy = -1;
        int pwr_en = -1;
    };

    struct ProductIdentity
    {
        const char* long_name = "Trail Mate Tab5";
        const char* short_name = "TAB5";
        const char* ble_name = "TrailMate-Tab5";
    };

    I2cBus sys_i2c{};
    I2cBus ext_i2c{};
    UartPort gps_uart{};
    UartPort rs485_uart{};
    SdmmcPins sdmmc{};
    AudioI2sPins audio_i2s{};
    LoRaModulePins lora_module{};
    int lcd_backlight = -1;
    int touch_int = -1;
    bool has_display = false;
    bool has_touch = false;
    bool has_audio = false;
    bool has_sdcard = false;
    bool has_gps_uart = false;
    bool has_rs485_uart = false;
    bool has_lora = false;
    bool has_m5bus_lora_module_routing = false;
    ProductIdentity identity{};
};

inline constexpr BoardProfile kBoardProfile{
    {0, 31, 32},
    {1, 53, 54},
    {2, 6, 7, -1, 38400},
    {1, 20, 21, 34, 0},
    {39, 40, 41, 42, 44, 43},
    {27, 30, 29, 26, 28},
    {{2, 5, 19, 18}, 35, 45, 16, -1, -1},
    22,
    23,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
    {"Trail Mate Tab5", "TAB5", "TrailMate-Tab5"},
};

} // namespace boards::tab5
