#pragma once

#include <cstdint>

#include "device/host_types.h"

namespace device
{

enum class CapabilityKind : std::uint8_t
{
    None,
    Lora,
    Gps,
    Ble,
    HostLink,
    Storage,
    Display,
    Input,
    Battery,
    Network,
    Audio,
    MapStorage,
};

enum class CapabilityState : std::uint8_t
{
    Unsupported,
    Absent,
    Present,
    Unbound,
    Initializing,
    Ready,
    Degraded,
    Simulated,
    Error,
};

enum class RadioLinkMode : std::uint8_t
{
    None,
    LocalRadio,
    PacketProxy,
    CommandProxy,
};

enum class GpsLinkMode : std::uint8_t
{
    None,
    LocalUart,
    RawStreamProxy,
    FixProxy,
    CommandProxy,
    Simulated,
};

struct CapabilityStatus
{
    CapabilityKind kind = CapabilityKind::None;
    CapabilityState state = CapabilityState::Unsupported;
    HostKind endpoint_host = HostKind::None;
};

const char* to_string(CapabilityKind kind) noexcept;
const char* to_string(CapabilityState state) noexcept;
const char* to_string(RadioLinkMode mode) noexcept;
const char* to_string(GpsLinkMode mode) noexcept;

} // namespace device
