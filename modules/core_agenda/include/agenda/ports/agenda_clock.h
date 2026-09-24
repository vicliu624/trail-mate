#pragma once

#include <cstdint>

namespace agenda
{
struct ClockSample
{
    int64_t calendar_seconds = 0;
    uint64_t monotonic_seconds = 0;
    uint32_t revision = 0; // Changes when timezone/time authority is reconfigured.
    bool valid = false;
};

class IAgendaClock
{
  public:
    virtual ~IAgendaClock() = default;
    virtual ClockSample sample() const = 0;
};
} // namespace agenda
