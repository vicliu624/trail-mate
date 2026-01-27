#pragma once

#include <Arduino.h>
#include "board/GpsBoard.h"

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
    uint8_t satellites() const;
    bool syncTime(uint32_t gps_task_interval_ms);

  private:
    GpsBoard* board_ = nullptr;
};

} // namespace hal
