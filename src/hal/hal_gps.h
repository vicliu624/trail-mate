#pragma once

#include <Arduino.h>

class TLoRaPagerBoard;

namespace hal {

class HalGps
{
public:
    void begin(TLoRaPagerBoard &board);
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
    TLoRaPagerBoard *board_ = nullptr;
};

}  // namespace hal
