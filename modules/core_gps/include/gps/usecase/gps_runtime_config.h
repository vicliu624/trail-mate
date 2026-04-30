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

struct ExternalNmeaRuntimeConfig
{
    uint8_t output_hz = 0;
    uint8_t sentence_mask = 0;
};

class GpsRuntimeConfig
{
  public:
    const GnssRuntimeConfig& gnssConfig() const { return gnss_config_; }
    const ExternalNmeaRuntimeConfig& externalNmeaConfig() const { return external_nmea_config_; }

    bool hasPendingGnssConfig() const { return gnss_config_pending_; }
    bool hasPendingExternalNmeaConfig() const { return external_nmea_config_pending_; }

    void setGnssConfig(uint8_t mode, uint8_t sat_mask);
    void setExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask);

    void markGnssConfigApplied();
    void markExternalNmeaConfigApplied();

  private:
    GnssRuntimeConfig gnss_config_{};
    ExternalNmeaRuntimeConfig external_nmea_config_{};
    bool gnss_config_pending_ = false;
    bool external_nmea_config_pending_ = false;
};

} // namespace gps
