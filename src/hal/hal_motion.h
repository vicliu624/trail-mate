#pragma once

#include "bosch/BoschParseCallbackManager.hpp"
#include <Arduino.h>
#include <SensorBHI260AP.hpp>
#include "board/MotionBoard.h"

namespace hal
{

class HalMotion
{
  public:
    void begin(MotionBoard& board);
    bool isReady() const;
    bool configure(uint8_t sensor_id, uint8_t interrupt_ctrl,
                   SensorDataParseCallback callback, void* user_data);
    void removeCallback(uint8_t sensor_id, SensorDataParseCallback callback);
    void attachInterrupt(void (*isr)());
    void detachInterrupt();
    void update();

  private:
    MotionBoard* board_ = nullptr;
};

} // namespace hal
