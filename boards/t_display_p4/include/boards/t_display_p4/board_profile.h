#pragma once

#include <cstdint>

namespace boards::t_display_p4
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

    I2cBus sys_i2c{};
    I2cBus ext_i2c{};
    UartPort gps_uart{};
    SdmmcPins sdmmc{};
    AudioI2sPins audio_i2s{};
    LoRaModulePins lora{};
    int boot = -1;
    int expander_int = -1;
    int lcd_backlight = -1;
    bool has_display = false;
    bool has_touch = false;
    bool has_audio = false;
    bool has_sdcard = false;
    bool has_gps_uart = false;
    bool has_lora = false;
    bool uses_io_expander_for_lora = false;
};

inline constexpr BoardProfile kBoardProfile{
    {0, 7, 8},
    {1, 20, 21},
    {1, 22, 23, -1},
    {39, 40, 41, 42, 44, 43},
    {12, 13, 9, 10, 11},
    {{1, 2, 4, 3}, 24, -1, -1, 6, -1},
    35,
    5,
    51,
    true,
    true,
    true,
    true,
    true,
    true,
    true,
};

} // namespace boards::t_display_p4
