#pragma once

#include <cstdint>

namespace platform::ui::orientation
{

struct HeadingState
{
    bool available = false;
    bool sensor_ready = false;
    float heading_deg = 0.0f;
    int16_t raw_x = 0;
    int16_t raw_y = 0;
    int16_t raw_z = 0;
    uint32_t last_update_ms = 0;
};

HeadingState get_heading();
void ensure_heading_runtime();

} // namespace platform::ui::orientation
