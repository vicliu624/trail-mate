#pragma once

#include <cstdint>

namespace display
{

enum class Driver
{
    Unknown = 0,
    ST7796,
    ST7789V2,
};

struct ScreenSize
{
    int width;
    int height;
};

struct Config
{
    Driver driver;
    ScreenSize screen;
};

Config get_config();

} // namespace display
