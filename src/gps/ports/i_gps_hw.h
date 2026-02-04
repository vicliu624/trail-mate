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
    virtual bool hasAltitude() const = 0;
    virtual double altitude() const = 0;
    virtual bool hasSpeed() const = 0;
    virtual double speed() const = 0;
    virtual bool hasCourse() const = 0;
    virtual double course() const = 0;
    virtual uint8_t satellites() const = 0;
    virtual bool syncTime(uint32_t gps_task_interval_ms) = 0;
    virtual bool applyGnssConfig(uint8_t mode, uint8_t sat_mask) = 0;
    virtual bool applyNmeaConfig(uint8_t output_hz, uint8_t sentence_mask) = 0;
};

} // namespace gps
