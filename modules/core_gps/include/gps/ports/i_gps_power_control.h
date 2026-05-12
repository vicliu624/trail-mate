#pragma once

#include <cstdint>

namespace gps
{

enum class PowerState : uint8_t
{
    Off,
    Starting,
    On,
    Stopping,
    Error,
};

class IGpsPowerControl
{
  public:
    virtual ~IGpsPowerControl() = default;

    virtual bool setPower(bool enabled) = 0;
    virtual PowerState powerState() const = 0;
};

} // namespace gps
