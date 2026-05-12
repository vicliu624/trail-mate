#pragma once

#include <cstdint>

namespace gps
{

enum class TimeSourceKind : uint8_t
{
    None = 0,
    System,
    Gps,
    Host,
    Rtc,
    Test,
};

struct TimeSyncStatus
{
    bool epoch_valid = false;
    bool synced = false;
    TimeSourceKind source = TimeSourceKind::None;
    uint64_t epoch_seconds = 0;
    uint32_t observed_at_ms = 0;
};

} // namespace gps
