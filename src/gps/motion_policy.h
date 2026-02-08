#pragma once

#include "domain/motion_config.h"
#include "ports/i_motion_hw.h"
#include <Arduino.h>

namespace gps
{

class MotionPolicy
{
  public:
    bool begin(IMotionHardware& motion, const MotionConfig& config);
    bool isEnabled() const { return enabled_; }
    uint32_t taskIntervalMs() const { return config_.task_interval_ms; }
    const MotionConfig& config() const { return config_; }
    uint32_t lastMotionMs() const { return enabled_ ? last_motion_ms_ : 0; }
    bool hasRecentMotion(uint32_t now_ms, uint32_t window_ms) const;

    void onSensorInterrupt();
    bool shouldUpdateSensor(uint32_t now_ms);
    void markSensorUpdated(uint32_t now_ms);
    bool shouldEnableGps(uint32_t now_ms);

  private:
    static void IRAM_ATTR sensorInterruptHandler();
    static void motionEventCallback(uint8_t sensor_id, uint8_t* data, uint32_t size,
                                    uint64_t* timestamp, void* user_data);

    MotionConfig config_{};
    IMotionHardware* motion_ = nullptr;
    bool enabled_ = false;

    volatile bool sensor_irq_pending_ = false;
    volatile bool motion_event_pending_ = false;
    uint32_t last_motion_ms_ = 0;
    uint32_t last_sensor_poll_ms_ = 0;
};

} // namespace gps
