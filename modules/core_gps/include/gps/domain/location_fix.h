#pragma once

#include "gps/domain/gps_state.h"

#include <cstdint>

namespace gps
{

struct LocationFix
{
    bool valid = false;
    double latitude = 0.0;
    double longitude = 0.0;
    float altitude_m = 0.0f;
    float speed_mps = 0.0f;
    float course_deg = 0.0f;
    bool has_altitude = false;
    bool has_speed = false;
    bool has_course = false;
    uint8_t satellites = 0;
    float hdop = 0.0f;
    uint64_t epoch_seconds = 0;
    uint32_t observed_at_ms = 0;
};

inline LocationFix locationFixFromGpsState(const GpsState& state,
                                           uint32_t observed_at_ms = 0,
                                           uint64_t epoch_seconds = 0,
                                           float hdop = 0.0f)
{
    LocationFix fix{};
    fix.valid = state.valid;
    fix.latitude = state.lat;
    fix.longitude = state.lng;
    fix.altitude_m = static_cast<float>(state.alt_m);
    fix.speed_mps = static_cast<float>(state.speed_mps);
    fix.course_deg = static_cast<float>(state.course_deg);
    fix.has_altitude = state.has_alt;
    fix.has_speed = state.has_speed;
    fix.has_course = state.has_course;
    fix.satellites = state.satellites;
    fix.hdop = hdop;
    fix.epoch_seconds = epoch_seconds;
    fix.observed_at_ms = observed_at_ms;
    return fix;
}

inline GpsState gpsStateFromLocationFix(const LocationFix& fix)
{
    GpsState state{};
    state.valid = fix.valid;
    state.lat = fix.latitude;
    state.lng = fix.longitude;
    state.alt_m = fix.has_altitude ? static_cast<double>(fix.altitude_m) : 0.0;
    state.speed_mps = fix.has_speed ? static_cast<double>(fix.speed_mps) : 0.0;
    state.course_deg = fix.has_course ? static_cast<double>(fix.course_deg) : 0.0;
    state.has_alt = fix.has_altitude;
    state.has_speed = fix.has_speed;
    state.has_course = fix.has_course;
    state.satellites = fix.satellites;
    state.age = 0;
    return state;
}

} // namespace gps
