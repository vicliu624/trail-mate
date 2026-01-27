#include "gps/infra/hal_motion_adapter.h"
#include "board/MotionBoard.h"

namespace gps
{

void HalMotionAdapter::begin(MotionBoard& board)
{
    hal_motion_.begin(board);
}

bool HalMotionAdapter::isReady() const
{
    return hal_motion_.isReady();
}

bool HalMotionAdapter::configure(uint8_t sensor_id, uint8_t interrupt_ctrl,
                                 SensorDataParseCallback callback, void* user_data)
{
    return hal_motion_.configure(sensor_id, interrupt_ctrl, callback, user_data);
}

void HalMotionAdapter::removeCallback(uint8_t sensor_id, SensorDataParseCallback callback)
{
    hal_motion_.removeCallback(sensor_id, callback);
}

void HalMotionAdapter::attachInterrupt(void (*isr)())
{
    hal_motion_.attachInterrupt(isr);
}

void HalMotionAdapter::detachInterrupt()
{
    hal_motion_.detachInterrupt();
}

void HalMotionAdapter::update()
{
    hal_motion_.update();
}

} // namespace gps
