#include "hal/hal_motion.h"

#include "board/MotionBoard.h"
#include "pins_arduino.h"

namespace hal
{

void HalMotion::begin(MotionBoard& board)
{
    board_ = &board;
}

bool HalMotion::isReady() const
{
    return board_ != nullptr && board_->isSensorReady();
}

bool HalMotion::configure(uint8_t sensor_id, uint8_t interrupt_ctrl,
                          SensorDataParseCallback callback, void* user_data)
{
    if (board_ == nullptr)
    {
        return false;
    }

    bool configured = board_->getMotionSensor().configure(sensor_id, 1.0f, 0);
    if (!configured)
    {
        log_w("Motion detect configure failed (sensor_id=%u)", sensor_id);
        return false;
    }

    board_->getMotionSensor().onResultEvent(
        static_cast<SensorBHI260AP::BoschSensorID>(sensor_id),
        callback,
        user_data);
    board_->getMotionSensor().setInterruptCtrl(interrupt_ctrl);
    return true;
}

void HalMotion::removeCallback(uint8_t sensor_id, SensorDataParseCallback callback)
{
    if (board_ == nullptr)
    {
        return;
    }
    board_->getMotionSensor().removeResultEvent(
        static_cast<SensorBHI260AP::BoschSensorID>(sensor_id),
        callback);
}

void HalMotion::attachInterrupt(void (*isr)())
{
    ::attachInterrupt(digitalPinToInterrupt(SENSOR_INT), isr, RISING);
}

void HalMotion::detachInterrupt()
{
    ::detachInterrupt(digitalPinToInterrupt(SENSOR_INT));
}

void HalMotion::update()
{
    if (board_ == nullptr)
    {
        return;
    }
    board_->getMotionSensor().update();
}

} // namespace hal
