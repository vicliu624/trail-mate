#pragma once

#include <cstdint>

namespace gps
{

struct GpsJitterFilterConfig
{
    float still_speed_max_mps = 1.0f;
    float max_speed_mps = 15.0f;
    float accel_max_mps2 = 4.0f;
    float margin_mps = 0.5f;
    uint32_t stationary_window_ms = 4000;
    uint32_t reset_after_ms = 300000;
    uint8_t max_rejects = 3;
};

struct GpsJitterDecision
{
    bool accepted = false;
    bool forced = false;
    bool stationary = false;
    float dt_s = 0.0f;
    float distance_m = 0.0f;
    float v_gps = 0.0f;
    float v_max = 0.0f;
    uint8_t reject_count = 0;
};

class GpsJitterFilter
{
  public:
    GpsJitterFilter() = default;
    explicit GpsJitterFilter(const GpsJitterFilterConfig& cfg)
        : cfg_(cfg)
    {
    }

    void reset();
    const GpsJitterFilterConfig& config() const { return cfg_; }
    void setConfig(const GpsJitterFilterConfig& cfg) { cfg_ = cfg; }

    GpsJitterDecision update(double lat, double lon, uint32_t now_ms, uint32_t last_motion_ms);

  private:
    static double haversine_m(double lat1, double lon1, double lat2, double lon2);
    bool isStationary(uint32_t now_ms, uint32_t last_motion_ms) const;

    GpsJitterFilterConfig cfg_{};
    bool has_fix_ = false;
    double last_lat_ = 0.0;
    double last_lon_ = 0.0;
    uint32_t last_ms_ = 0;
    float last_speed_mps_ = 0.0f;
    uint8_t reject_count_ = 0;
};

} // namespace gps
