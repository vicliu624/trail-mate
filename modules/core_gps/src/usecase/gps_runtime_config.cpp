#include "gps/usecase/gps_runtime_config.h"

namespace gps
{

void GpsRuntimeConfig::setGnssConfig(uint8_t mode, uint8_t sat_mask)
{
    gnss_config_.mode = mode;
    gnss_config_.sat_mask = sat_mask;
    gnss_config_pending_ = true;
}

void GpsRuntimeConfig::setNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    nmea_config_.output_hz = output_hz;
    nmea_config_.sentence_mask = sentence_mask;
    nmea_config_pending_ = true;
}

void GpsRuntimeConfig::markGnssConfigApplied()
{
    gnss_config_pending_ = false;
}

void GpsRuntimeConfig::markNmeaConfigApplied()
{
    nmea_config_pending_ = false;
}

} // namespace gps
