#pragma once

#include "bosch/BoschParseCallbackManager.hpp"

namespace gps
{

class IMotionHardware
{
  public:
    virtual ~IMotionHardware() = default;
    virtual bool isReady() const = 0;
    virtual bool configure(uint8_t sensor_id, uint8_t interrupt_ctrl,
                           SensorDataParseCallback callback, void* user_data) = 0;
    virtual void removeCallback(uint8_t sensor_id, SensorDataParseCallback callback) = 0;
    virtual void attachInterrupt(void (*isr)()) = 0;
    virtual void detachInterrupt() = 0;
    virtual void update() = 0;
};

} // namespace gps
