#include "gps/usecase/gps_runtime_config.h"

namespace gps
{

void GpsRuntimeConfig::setGnssConfig(uint8_t mode, uint8_t sat_mask)
{
    gnss_config_.mode = mode;
    gnss_config_.sat_mask = sat_mask;
    gnss_config_pending_ = true;
}

void GpsRuntimeConfig::setExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    external_nmea_config_.output_hz = output_hz;
    external_nmea_config_.sentence_mask = sentence_mask;
    external_nmea_config_pending_ = true;
}

void GpsRuntimeConfig::markGnssConfigApplied()
{
    gnss_config_pending_ = false;
}

void GpsRuntimeConfig::markExternalNmeaConfigApplied()
{
    external_nmea_config_pending_ = false;
}

} // namespace gps
