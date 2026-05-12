#pragma once

#include "gps/domain/gnss_satellite.h"

#include <array>

namespace gps
{

struct SatelliteSnapshot
{
    std::array<GnssSatInfo, kMaxGnssSats> satellites{};
    std::size_t count = 0;
    GnssStatus status{};
};

} // namespace gps
