#pragma once

#include "../gps/domain/gnss_satellite.h"
#include "board/GpsBoard.h"
#include <Arduino.h>

namespace hal
{

class HalGps
{
  public:
    void begin(GpsBoard& board);
    bool isReady() const;
    bool init();
    void powerOn();
    void powerOff();
    uint32_t loop();
    bool hasFix() const;
    double latitude() const;
    double longitude() const;
    bool hasAltitude() const;
    double altitude() const;
    bool hasSpeed() const;
    double speed() const;
    bool hasCourse() const;
    double course() const;
    uint8_t satellites() const;
    size_t getSatellites(gps::GnssSatInfo* out, size_t max) const;
    gps::GnssStatus getGnssStatus() const;
    bool syncTime(uint32_t gps_task_interval_ms);
    bool applyGnssConfig(uint8_t mode, uint8_t sat_mask);
    bool applyNmeaConfig(uint8_t output_hz, uint8_t sentence_mask);

  private:
    GpsBoard* board_ = nullptr;
};

} // namespace hal
