#pragma once

#include <stdint.h>

namespace gps
{

struct GpsState
{
    double lat = 0.0;
    double lng = 0.0;
    double alt_m = 0.0;
    double speed_mps = 0.0;
    double course_deg = 0.0;
    uint8_t satellites = 0;
    bool valid = false;
    bool has_alt = false;
    bool has_speed = false;
    bool has_course = false;
    uint32_t age = 0;
};

} // namespace gps
