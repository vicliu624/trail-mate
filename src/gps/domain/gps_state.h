#pragma once

#include <stdint.h>

namespace gps
{

struct GpsState
{
    double lat = 0.0;
    double lng = 0.0;
    uint8_t satellites = 0;
    bool valid = false;
    uint32_t age = 0;
};

} // namespace gps
