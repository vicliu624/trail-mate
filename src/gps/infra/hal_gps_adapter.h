#pragma once

#include "../../hal/hal_gps.h"
#include "../ports/i_gps_hw.h"
#include "board/GpsBoard.h"

namespace gps
{

class HalGpsAdapter : public IGpsHardware
{
  public:
    void begin(GpsBoard& board);

    bool isReady() const override;
    bool init() override;
    void powerOn() override;
    void powerOff() override;
    uint32_t loop() override;
    bool hasFix() const override;
    double latitude() const override;
    double longitude() const override;
    bool hasAltitude() const override;
    double altitude() const override;
    bool hasSpeed() const override;
    double speed() const override;
    bool hasCourse() const override;
    double course() const override;
    uint8_t satellites() const override;
    bool syncTime(uint32_t gps_task_interval_ms) override;
    bool applyGnssConfig(uint8_t mode, uint8_t sat_mask) override;
    bool applyNmeaConfig(uint8_t output_hz, uint8_t sentence_mask) override;

  private:
    hal::HalGps hal_gps_{};
};

} // namespace gps
