#pragma once

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"

#include <stdint.h>

namespace ui::device
{

enum class CapabilityDisplayState : uint8_t
{
    Unsupported,
    Off,
    Starting,
    Ready,
    Degraded,
    Error,
    Simulated,
};

struct DeviceStatusSnapshot
{
    ui::SnapshotHeader header;

    CapabilityDisplayState lora = CapabilityDisplayState::Unsupported;
    CapabilityDisplayState gps = CapabilityDisplayState::Unsupported;
    CapabilityDisplayState ble = CapabilityDisplayState::Unsupported;
    CapabilityDisplayState storage = CapabilityDisplayState::Unsupported;
    CapabilityDisplayState battery = CapabilityDisplayState::Unsupported;

    bool time_synced = false;
    uint64_t epoch_seconds = 0;

    ui::FixedText<32> active_protocol;
    ui::FixedText<32> region;
    ui::FixedText<32> modem_preset;

    ui::FixedText<64> status_line;
};

} // namespace ui::device
