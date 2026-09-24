#pragma once

#include <cmath>
#include <cstdint>

namespace ui::map_geo
{
constexpr double kCoordPi = 3.14159265358979323846;
constexpr double kCoordA = 6378245.0;
constexpr double kCoordEe = 0.00669342162296594323;

inline bool coord_out_of_china(double lat, double lon)
{
    return (lon < 72.004 || lon > 137.8347 || lat < 0.8293 || lat > 55.8271);
}
inline double coord_transform_lat(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y +
                 0.2 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kCoordPi) + 20.0 * std::sin(2.0 * x * kCoordPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * kCoordPi) + 40.0 * std::sin(y / 3.0 * kCoordPi)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * kCoordPi) + 320 * std::sin(y * kCoordPi / 30.0)) * 2.0 / 3.0;
    return ret;
}
inline double coord_transform_lon(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y +
                 0.1 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kCoordPi) + 20.0 * std::sin(2.0 * x * kCoordPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * kCoordPi) + 40.0 * std::sin(x / 3.0 * kCoordPi)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * kCoordPi) + 300.0 * std::sin(x / 30.0 * kCoordPi)) * 2.0 / 3.0;
    return ret;
}
inline void wgs84_to_gcj02(double lat, double lon, double& out_lat, double& out_lon)
{
    if (coord_out_of_china(lat, lon))
    {
        out_lat = lat;
        out_lon = lon;
        return;
    }
    double dlat = coord_transform_lat(lon - 105.0, lat - 35.0);
    double dlon = coord_transform_lon(lon - 105.0, lat - 35.0);
    double radlat = lat / 180.0 * kCoordPi;
    double magic = std::sin(radlat);
    magic = 1 - kCoordEe * magic * magic;
    double sqrt_magic = std::sqrt(magic);
    dlat = (dlat * 180.0) / ((kCoordA * (1 - kCoordEe)) / (magic * sqrt_magic) * kCoordPi);
    dlon = (dlon * 180.0) / (kCoordA / sqrt_magic * std::cos(radlat) * kCoordPi);
    out_lat = lat + dlat;
    out_lon = lon + dlon;
}
inline void gcj02_to_bd09(double lat, double lon, double& out_lat, double& out_lon)
{
    double z = std::sqrt(lon * lon + lat * lat) + 0.00002 * std::sin(lat * kCoordPi);
    double theta = std::atan2(lat, lon) + 0.000003 * std::cos(lon * kCoordPi);
    out_lon = z * std::cos(theta) + 0.0065;
    out_lat = z * std::sin(theta) + 0.006;
}
inline bool valid(double latitude, double longitude)
{
    return std::isfinite(latitude) && std::isfinite(longitude) &&
           latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0 && longitude <= 180.0;
}
inline double wrap_longitude(double longitude)
{
    double wrapped = std::fmod(longitude + 180.0, 360.0);
    if (wrapped < 0.0) wrapped += 360.0;
    return wrapped - 180.0;
}

// The forward formulas are the existing viewport formulas, shared unchanged
// with inverse selection so drawing and returned coordinates cannot drift.
inline bool transform(double latitude, double longitude, uint8_t system, double& out_lat, double& out_lon)
{
    if (!valid(latitude, longitude) || system > 2) return false;
    out_lat = latitude;
    out_lon = longitude;
    if (system == 0) return true;
    wgs84_to_gcj02(latitude, longitude, out_lat, out_lon);
    if (system == 2) gcj02_to_bd09(out_lat, out_lon, out_lat, out_lon);
    return true;
}

inline bool inverse(double latitude, double longitude, uint8_t system, double& out_lat, double& out_lon)
{
    if (!valid(latitude, longitude) || system > 2) return false;
    double candidate_lat = latitude;
    double candidate_lon = longitude;
    // Fixed bound: a coordinate conversion must never stall the UI loop.
    for (unsigned iteration = 0; iteration < 12; ++iteration)
    {
        double projected_lat = 0.0;
        double projected_lon = 0.0;
        if (!transform(candidate_lat, candidate_lon, system, projected_lat, projected_lon)) return false;
        const double delta_lat = latitude - projected_lat;
        const double delta_lon = wrap_longitude(longitude - projected_lon);
        if (std::fabs(delta_lat) < 1e-8 && std::fabs(delta_lon) < 1e-8)
        {
            out_lat = candidate_lat;
            out_lon = candidate_lon;
            return true;
        }
        candidate_lat += delta_lat;
        candidate_lon = wrap_longitude(candidate_lon + delta_lon);
    }
    return false; // Discontinuities / non-convergence must not yield a wrong fix.
}
} // namespace ui::map_geo
