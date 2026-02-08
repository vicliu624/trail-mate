#include "gps/usecase/gps_jitter_filter.h"

#include <algorithm>
#include <cmath>

namespace gps
{
namespace
{
double deg2rad(double deg)
{
    return deg * 0.017453292519943295; // pi / 180
}
} // namespace

void GpsJitterFilter::reset()
{
    has_fix_ = false;
    last_lat_ = 0.0;
    last_lon_ = 0.0;
    last_ms_ = 0;
    last_speed_mps_ = 0.0f;
    reject_count_ = 0;
}

GpsJitterDecision GpsJitterFilter::update(double lat, double lon, uint32_t now_ms, uint32_t last_motion_ms)
{
    GpsJitterDecision decision{};
    decision.reject_count = reject_count_;

    if (!has_fix_)
    {
        has_fix_ = true;
        last_lat_ = lat;
        last_lon_ = lon;
        last_ms_ = now_ms;
        last_speed_mps_ = 0.0f;
        reject_count_ = 0;
        decision.accepted = true;
        return decision;
    }

    uint32_t dt_ms = now_ms - last_ms_;
    if (dt_ms == 0)
    {
        decision.accepted = true;
        return decision;
    }

    if (cfg_.reset_after_ms > 0 && dt_ms >= cfg_.reset_after_ms)
    {
        has_fix_ = true;
        last_lat_ = lat;
        last_lon_ = lon;
        last_ms_ = now_ms;
        last_speed_mps_ = 0.0f;
        reject_count_ = 0;
        decision.accepted = true;
        decision.forced = true;
        decision.dt_s = static_cast<float>(dt_ms) / 1000.0f;
        return decision;
    }

    decision.dt_s = static_cast<float>(dt_ms) / 1000.0f;
    decision.distance_m = static_cast<float>(haversine_m(last_lat_, last_lon_, lat, lon));
    decision.v_gps = (decision.dt_s > 0.0f) ? (decision.distance_m / decision.dt_s) : 0.0f;

    decision.stationary = isStationary(now_ms, last_motion_ms);
    float v_max = cfg_.max_speed_mps;

    if (decision.stationary)
    {
        v_max = cfg_.still_speed_max_mps;
    }
    else
    {
        float accel_bound = last_speed_mps_ + cfg_.accel_max_mps2 * decision.dt_s + cfg_.margin_mps;
        v_max = std::min(cfg_.max_speed_mps, accel_bound);
    }

    decision.v_max = v_max;

    if (decision.v_gps > v_max)
    {
        reject_count_++;
        decision.reject_count = reject_count_;
        if (reject_count_ > cfg_.max_rejects)
        {
            decision.accepted = true;
            decision.forced = true;
            reject_count_ = 0;
        }
        else
        {
            decision.accepted = false;
            return decision;
        }
    }
    else
    {
        decision.accepted = true;
        reject_count_ = 0;
    }

    last_lat_ = lat;
    last_lon_ = lon;
    last_ms_ = now_ms;
    last_speed_mps_ = std::min(decision.v_gps, cfg_.max_speed_mps);
    return decision;
}

double GpsJitterFilter::haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double R = 6371000.0;
    const double dlat = deg2rad(lat2 - lat1);
    const double dlon = deg2rad(lon2 - lon1);
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(deg2rad(lat1)) * std::cos(deg2rad(lat2)) *
                         std::sin(dlon / 2) * std::sin(dlon / 2);
    const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return R * c;
}

bool GpsJitterFilter::isStationary(uint32_t now_ms, uint32_t last_motion_ms) const
{
    if (last_motion_ms == 0)
    {
        return false;
    }
    return (now_ms - last_motion_ms) >= cfg_.stationary_window_ms;
}

} // namespace gps
