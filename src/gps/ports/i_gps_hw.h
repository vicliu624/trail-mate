#pragma once

#include <stdint.h>

namespace gps
{

class IGpsHardware
{
  public:
    virtual ~IGpsHardware() = default;
    virtual bool isReady() const = 0;
    virtual bool init() = 0;
    virtual void powerOn() = 0;
    virtual void powerOff() = 0;
    virtual uint32_t loop() = 0;
    virtual bool hasFix() const = 0;
    virtual double latitude() const = 0;
    virtual double longitude() const = 0;
    virtual uint8_t satellites() const = 0;
    virtual bool syncTime(uint32_t gps_task_interval_ms) = 0;
};

} // namespace gps
