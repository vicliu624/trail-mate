#pragma once

#include <cstdint>

namespace device
{

enum class HostKind : std::uint8_t
{
    None,
    Esp32,
    Nrf52,
    Linux,
    External,
    Test,
};

} // namespace device
