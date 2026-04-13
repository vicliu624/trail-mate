#pragma once

#include <cstdint>

namespace boards::tdeck_pro
{

struct BoardProfile
{
    struct ProductIdentity
    {
        const char* long_name;
        const char* short_name;
        const char* ble_name;
    };

    struct I2cPins
    {
        int sda;
        int scl;
    };

    struct SpiPins
    {
        int sck;
        int mosi;
        int miso;
    };

    struct UartPins
    {
        int tx;
        int rx;
        uint32_t baud;
    };

    struct EpdPins
    {
        int cs;
        int dc;
        int rst;
        int busy;
    };

    struct TouchPins
    {
        int int_pin;
        int rst;
        uint8_t i2c_addr;
    };

    struct KeyboardPins
    {
        int int_pin;
        int led;
        uint8_t i2c_addr;
        uint8_t rows;
        uint8_t cols;
    };

    struct LoraPins
    {
        int cs;
        int busy;
        int rst;
        int irq;
        int enable;
    };

    struct GpsPins
    {
        int enable;
        int pps;
        UartPins uart;
    };

    struct MotionPins
    {
        int enable_1v8;
        int int_pin;
        uint8_t i2c_addr;
    };

    struct SdPins
    {
        int cs;
    };

    struct OptionalModulePins
    {
        bool has_a7682e;
        bool has_pcm512a;
        int a7682e_enable;
        int a7682e_pwrkey;
        int a7682e_rst;
        int a7682e_ri;
        int a7682e_dtr;
        UartPins a7682e_uart;
        int i2s_bclk;
        int i2s_lrc;
        int i2s_dout;
    };

    ProductIdentity identity;
    I2cPins i2c;
    SpiPins spi;
    EpdPins epd;
    TouchPins touch;
    KeyboardPins keyboard;
    LoraPins lora;
    GpsPins gps;
    MotionPins motion;
    SdPins sd;
    OptionalModulePins optional;
    int motor_pin;
    int keyboard_default_brightness;
    int screen_width;
    int screen_height;
};

inline BoardProfile makeA7682EProfile()
{
    BoardProfile profile{};
    profile.identity.long_name = "Trail Mate T-Deck Pro A7682E";
    profile.identity.short_name = "TDP4G";
    profile.identity.ble_name = "TrailMate-TDeckPro-4G";

    profile.i2c.sda = 13;
    profile.i2c.scl = 14;

    profile.spi.sck = 36;
    profile.spi.mosi = 33;
    profile.spi.miso = 47;

    profile.epd.cs = 34;
    profile.epd.dc = 35;
    profile.epd.rst = -1;
    profile.epd.busy = 37;

    profile.touch.int_pin = 12;
    profile.touch.rst = 45;
    profile.touch.i2c_addr = 0x1A;

    profile.keyboard.int_pin = 15;
    profile.keyboard.led = 42;
    profile.keyboard.i2c_addr = 0x34;
    profile.keyboard.rows = 4;
    profile.keyboard.cols = 10;

    profile.lora.cs = 3;
    profile.lora.busy = 6;
    profile.lora.rst = 4;
    profile.lora.irq = 5;
    profile.lora.enable = 46;

    profile.gps.enable = 39;
    profile.gps.pps = 1;
    profile.gps.uart.tx = 43;
    profile.gps.uart.rx = 44;
    profile.gps.uart.baud = 38400;

    profile.motion.enable_1v8 = 38;
    profile.motion.int_pin = 21;
    profile.motion.i2c_addr = 0x28;

    profile.sd.cs = 48;

    profile.optional.has_a7682e = true;
    profile.optional.has_pcm512a = false;
    profile.optional.a7682e_enable = 41;
    profile.optional.a7682e_pwrkey = 40;
    profile.optional.a7682e_rst = 9;
    profile.optional.a7682e_ri = 7;
    profile.optional.a7682e_dtr = 8;
    profile.optional.a7682e_uart.tx = 11;
    profile.optional.a7682e_uart.rx = 10;
    profile.optional.a7682e_uart.baud = 115200;
    profile.optional.i2s_bclk = -1;
    profile.optional.i2s_lrc = -1;
    profile.optional.i2s_dout = -1;

    profile.motor_pin = 2;
    profile.keyboard_default_brightness = 96;
    profile.screen_width = 240;
    profile.screen_height = 320;
    return profile;
}

inline BoardProfile makePCM512AProfile()
{
    BoardProfile profile = makeA7682EProfile();
    profile.identity.long_name = "Trail Mate T-Deck Pro PCM512A";
    profile.identity.short_name = "TDPA";
    profile.identity.ble_name = "TrailMate-TDeckPro-Audio";

    profile.optional.has_a7682e = false;
    profile.optional.has_pcm512a = true;
    profile.optional.a7682e_enable = -1;
    profile.optional.a7682e_pwrkey = -1;
    profile.optional.a7682e_rst = -1;
    profile.optional.a7682e_ri = -1;
    profile.optional.a7682e_dtr = -1;
    profile.optional.a7682e_uart.tx = -1;
    profile.optional.a7682e_uart.rx = -1;
    profile.optional.a7682e_uart.baud = 0;
    profile.optional.i2s_bclk = 7;
    profile.optional.i2s_lrc = 9;
    profile.optional.i2s_dout = 8;
    profile.motor_pin = 40;
    return profile;
}

#if defined(TRAIL_MATE_TDECK_PRO_A7682E)
static const BoardProfile kBoardProfile = makeA7682EProfile();
#elif defined(TRAIL_MATE_TDECK_PRO_PCM512A)
static const BoardProfile kBoardProfile = makePCM512AProfile();
#else
#error "T-Deck Pro profile requires TRAIL_MATE_TDECK_PRO_A7682E or TRAIL_MATE_TDECK_PRO_PCM512A"
#endif

} // namespace boards::tdeck_pro
