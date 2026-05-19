#pragma once

#include <cstdint>

namespace device
{

enum class PlatformKind : std::uint8_t
{
    None,
    Esp32,
    Nrf52,
    Linux,
    Test,
};

} // namespace device
