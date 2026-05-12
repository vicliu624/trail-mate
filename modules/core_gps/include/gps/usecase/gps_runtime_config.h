#pragma once

#include <cstdint>

namespace gps
{

constexpr uint8_t kDefaultGnssSatelliteMask = 0x1 | 0x8 | 0x4;

enum class GpsReceiverProtocol : uint8_t
{
    Unknown = 0,
    NoTraffic = 1,
    Nmea = 2,
    Ubx = 3,
    OtherTraffic = 4,
};

inline const char* gpsReceiverProtocolName(GpsReceiverProtocol protocol)
{
    switch (protocol)
    {
    case GpsReceiverProtocol::Unknown:
        return "unknown";
    case GpsReceiverProtocol::NoTraffic:
        return "no_traffic";
    case GpsReceiverProtocol::Nmea:
        return "nmea";
    case GpsReceiverProtocol::Ubx:
        return "ubx";
    case GpsReceiverProtocol::OtherTraffic:
        return "other_traffic";
    }
    return "unknown";
}

struct GpsReceiverInitConfig
{
    uint32_t baud = 0;
    uint32_t probe_ms = 900;
    uint8_t profile = 0;
    uint8_t rxm_policy = 0;
    uint8_t gnss_policy = 0;
    uint8_t nmea_policy = 0;
};

struct GnssRuntimeConfig
{
    uint32_t baud = 9600;
    uint32_t update_interval_ms = 1000;
    uint8_t mode = 0;
    uint8_t sat_mask = kDefaultGnssSatelliteMask;
    bool nmea_enabled = true;
    bool ubx_enabled = false;
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
