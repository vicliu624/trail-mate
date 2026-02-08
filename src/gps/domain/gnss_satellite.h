#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gps
{

enum class GnssSystem : uint8_t
{
    GPS,
    GLN,
    GAL,
    BD,
    UNKNOWN
};

enum class GnssFix : uint8_t
{
    NOFIX = 0,
    FIX2D = 2,
    FIX3D = 3
};

struct GnssSatInfo
{
    uint16_t id = 0;
    GnssSystem sys = GnssSystem::UNKNOWN;
    uint16_t azimuth = 0;
    uint8_t elevation = 0;
    int8_t snr = -1;
    bool used = false;
};

struct GnssStatus
{
    uint8_t sats_in_use = 0;
    uint8_t sats_in_view = 0;
    float hdop = 0.0f;
    GnssFix fix = GnssFix::NOFIX;
};

constexpr size_t kMaxGnssSats = 32;

} // namespace gps
