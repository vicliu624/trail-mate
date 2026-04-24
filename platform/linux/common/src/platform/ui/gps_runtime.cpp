#include "platform/ui/gps_runtime.h"

#include <cmath>

namespace platform::ui::gps
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxMercatorLat = 85.05112878;
constexpr double kEquatorResolution = 156543.03392;

} // namespace

GpsState get_data()
{
    return {};
}

bool get_gnss_snapshot(GnssSatInfo*, std::size_t, std::size_t* out_count, GnssStatus* status)
{
    if (out_count)
    {
        *out_count = 0;
    }
    if (status)
    {
        *status = GnssStatus{};
    }
    return false;
}

uint32_t last_motion_ms()
{
    return 0;
}

bool is_enabled()
{
    return false;
}

bool is_powered()
{
    return false;
}

void set_collection_interval(uint32_t)
{
}

void set_power_strategy(uint8_t)
{
}

void set_gnss_config(uint8_t, uint8_t)
{
}

void set_nmea_config(uint8_t, uint8_t)
{
}

void set_motion_idle_timeout(uint32_t)
{
}

void set_motion_sensor_id(uint8_t)
{
}

void suspend_runtime()
{
}

void resume_runtime()
{
}

double calculate_map_resolution(int zoom, double lat)
{
    double clamped_lat = lat;
    if (clamped_lat > kMaxMercatorLat)
    {
        clamped_lat = kMaxMercatorLat;
    }
    else if (clamped_lat < -kMaxMercatorLat)
    {
        clamped_lat = -kMaxMercatorLat;
    }

    const double zoom_scale = std::pow(2.0, static_cast<double>(zoom));
    const double lat_rad = clamped_lat * kPi / 180.0;
    return (kEquatorResolution / zoom_scale) * std::cos(lat_rad);
}

} // namespace platform::ui::gps
