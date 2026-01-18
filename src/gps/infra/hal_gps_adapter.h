#pragma once

#include "../ports/i_gps_hw.h"
#include "../../hal/hal_gps.h"

class TLoRaPagerBoard;

namespace gps {

class HalGpsAdapter : public IGpsHardware
{
public:
    void begin(TLoRaPagerBoard &board);

    bool isReady() const override;
    bool init() override;
    void powerOn() override;
    void powerOff() override;
    uint32_t loop() override;
    bool hasFix() const override;
    double latitude() const override;
    double longitude() const override;
    uint8_t satellites() const override;
    bool syncTime(uint32_t gps_task_interval_ms) override;

private:
    hal::HalGps hal_gps_ {};
};

}  // namespace gps
