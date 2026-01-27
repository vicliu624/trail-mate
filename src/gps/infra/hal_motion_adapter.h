#pragma once

#include "../../hal/hal_motion.h"
#include "../ports/i_motion_hw.h"
#include "board/MotionBoard.h"

namespace gps
{

class HalMotionAdapter : public IMotionHardware
{
  public:
    void begin(MotionBoard& board);

    bool isReady() const override;
    bool configure(uint8_t sensor_id, uint8_t interrupt_ctrl,
                   SensorDataParseCallback callback, void* user_data) override;
    void removeCallback(uint8_t sensor_id, SensorDataParseCallback callback) override;
    void attachInterrupt(void (*isr)()) override;
    void detachInterrupt() override;
    void update() override;

  private:
    hal::HalMotion hal_motion_{};
};

} // namespace gps
