#pragma once

#include "motion_sensor_ids.h"
#include <stdint.h>

namespace gps
{

struct MotionConfig
{
    uint32_t idle_timeout_ms = 5 * 60 * 1000;
    uint32_t poll_interval_ms = 1000;
    uint32_t task_interval_ms = 200;
    uint8_t sensor_id = kAnyMotionLpWakeUp;
    uint8_t interrupt_ctrl = 0x40;
};

} // namespace gps
