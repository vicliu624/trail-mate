#pragma once

#include <cstdint>

namespace gps
{

using MotionEventCallback = void (*)(uint8_t sensor_id, uint8_t* data, uint32_t size,
                                     uint64_t* timestamp, void* user_data);

class IMotionHardware
{
  public:
    virtual ~IMotionHardware() = default;
    virtual bool isReady() const = 0;
    virtual bool configure(uint8_t sensor_id, uint8_t interrupt_ctrl,
                           MotionEventCallback callback, void* user_data) = 0;
    virtual void removeCallback(uint8_t sensor_id, MotionEventCallback callback) = 0;
    virtual void attachInterrupt(void (*isr)()) = 0;
    virtual void detachInterrupt() = 0;
    virtual void update() = 0;
};

} // namespace gps
