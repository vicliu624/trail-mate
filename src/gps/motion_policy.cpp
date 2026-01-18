#include "motion_policy.h"

namespace gps {

namespace {
MotionPolicy *g_instance = nullptr;
}

bool MotionPolicy::begin(IMotionHardware &motion, const MotionConfig &config)
{
    if (enabled_ && motion_ != nullptr) {
        motion_->removeCallback(config_.sensor_id, motionEventCallback);
        motion_->detachInterrupt();
    }

    motion_ = &motion;
    config_ = config;
    enabled_ = false;

    if (!motion_->isReady()) {
        return false;
    }

    bool configured = motion_->configure(
        config_.sensor_id,
        config_.interrupt_ctrl,
        motionEventCallback,
        this
    );
    if (!configured) {
        return false;
    }

    motion_->attachInterrupt(sensorInterruptHandler);

    last_motion_ms_ = 0;
    last_sensor_poll_ms_ = millis();
    g_instance = this;
    enabled_ = true;
    return true;
}

void MotionPolicy::onSensorInterrupt()
{
    sensor_irq_pending_ = true;
}

bool MotionPolicy::shouldUpdateSensor(uint32_t now_ms)
{
    if (sensor_irq_pending_) {
        sensor_irq_pending_ = false;
        return true;
    }
    return (now_ms - last_sensor_poll_ms_) >= config_.poll_interval_ms;
}

void MotionPolicy::markSensorUpdated(uint32_t now_ms)
{
    last_sensor_poll_ms_ = now_ms;
}

bool MotionPolicy::shouldEnableGps(uint32_t now_ms)
{
    if (!enabled_) {
        return false;
    }

    if (motion_event_pending_) {
        motion_event_pending_ = false;
    }

    return (last_motion_ms_ > 0) &&
           (now_ms - last_motion_ms_ < config_.idle_timeout_ms);
}

void IRAM_ATTR MotionPolicy::sensorInterruptHandler()
{
    if (g_instance != nullptr) {
        g_instance->onSensorInterrupt();
    }
}

void MotionPolicy::motionEventCallback(uint8_t sensor_id, uint8_t *data, uint32_t size,
                                       uint64_t *timestamp, void *user_data)
{
    (void)sensor_id;
    (void)data;
    (void)size;
    (void)timestamp;
    auto *policy = static_cast<MotionPolicy *>(user_data);
    if (policy == nullptr) {
        return;
    }
    policy->motion_event_pending_ = true;
    policy->last_motion_ms_ = millis();
}

}  // namespace gps
