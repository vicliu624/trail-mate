#pragma once

#include <cstdint>

#include "device/platform_types.h"

namespace device
{

struct BoardId
{
    const char* value = "";
};

struct BoardPackageRef
{
    BoardId id{};
    PlatformKind platform = PlatformKind::None;
    const char* package_path = "";
};

struct DisplayFacts
{
    bool present = false;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t rotation = 0;
    const char* driver = "";
};

struct InputFacts
{
    bool keyboard = false;
    bool buttons = false;
    bool touch = false;
    bool encoder = false;
    bool trackball = false;
};

} // namespace device
