#pragma once

#include <cstdint>

namespace gps
{

constexpr uint8_t kDefaultGnssSatelliteMask = 0x1 | 0x8 | 0x4;

struct GnssRuntimeConfig
{
    uint8_t mode = 0;
    uint8_t sat_mask = kDefaultGnssSatelliteMask;
};

struct NmeaRuntimeConfig
{
    uint8_t output_hz = 0;
    uint8_t sentence_mask = 0;
};

class GpsRuntimeConfig
{
  public:
    const GnssRuntimeConfig& gnssConfig() const { return gnss_config_; }
    const NmeaRuntimeConfig& nmeaConfig() const { return nmea_config_; }

    bool hasPendingGnssConfig() const { return gnss_config_pending_; }
    bool hasPendingNmeaConfig() const { return nmea_config_pending_; }

    void setGnssConfig(uint8_t mode, uint8_t sat_mask);
    void setNmeaConfig(uint8_t output_hz, uint8_t sentence_mask);

    void markGnssConfigApplied();
    void markNmeaConfigApplied();

  private:
    GnssRuntimeConfig gnss_config_{};
    NmeaRuntimeConfig nmea_config_{};
    bool gnss_config_pending_ = false;
    bool nmea_config_pending_ = false;
};

} // namespace gps
