#include "device/capability_types.h"

namespace device
{

const char* to_string(CapabilityKind kind) noexcept
{
    switch (kind)
    {
    case CapabilityKind::None:
        return "none";
    case CapabilityKind::Lora:
        return "lora";
    case CapabilityKind::Gps:
        return "gps";
    case CapabilityKind::Ble:
        return "ble";
    case CapabilityKind::HostLink:
        return "hostlink";
    case CapabilityKind::Storage:
        return "storage";
    case CapabilityKind::Display:
        return "display";
    case CapabilityKind::Input:
        return "input";
    case CapabilityKind::Battery:
        return "battery";
    case CapabilityKind::Network:
        return "network";
    case CapabilityKind::Audio:
        return "audio";
    case CapabilityKind::MapStorage:
        return "map_storage";
    }
    return "unknown";
}

const char* to_string(CapabilityState state) noexcept
{
    switch (state)
    {
    case CapabilityState::Unsupported:
        return "unsupported";
    case CapabilityState::Absent:
        return "absent";
    case CapabilityState::Present:
        return "present";
    case CapabilityState::Unbound:
        return "unbound";
    case CapabilityState::Initializing:
        return "initializing";
    case CapabilityState::Ready:
        return "ready";
    case CapabilityState::Degraded:
        return "degraded";
    case CapabilityState::Simulated:
        return "simulated";
    case CapabilityState::Error:
        return "error";
    }
    return "unknown";
}

const char* to_string(RadioLinkMode mode) noexcept
{
    switch (mode)
    {
    case RadioLinkMode::None:
        return "none";
    case RadioLinkMode::LocalRadio:
        return "local_radio";
    case RadioLinkMode::PacketProxy:
        return "packet_proxy";
    case RadioLinkMode::CommandProxy:
        return "command_proxy";
    }
    return "unknown";
}

const char* to_string(GpsLinkMode mode) noexcept
{
    switch (mode)
    {
    case GpsLinkMode::None:
        return "none";
    case GpsLinkMode::LocalUart:
        return "local_uart";
    case GpsLinkMode::RawStreamProxy:
        return "raw_stream_proxy";
    case GpsLinkMode::FixProxy:
        return "fix_proxy";
    case GpsLinkMode::CommandProxy:
        return "command_proxy";
    case GpsLinkMode::Simulated:
        return "simulated";
    }
    return "unknown";
}

} // namespace device
