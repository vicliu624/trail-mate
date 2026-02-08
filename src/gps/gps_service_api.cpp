#include "gps_service_api.h"
#include "usecase/gps_service.h"
#include <cmath>

namespace gps
{

GpsState gps_get_data()
{
    return GpsService::getInstance().getData();
}

bool gps_get_gnss_snapshot(gps::GnssSatInfo* out, size_t max, size_t* out_count, gps::GnssStatus* status)
{
    return GpsService::getInstance().getGnssSnapshot(out, max, out_count, status);
}

uint32_t gps_get_last_motion_ms()
{
    return GpsService::getInstance().getLastMotionMs();
}

void gps_set_collection_interval(uint32_t interval_ms)
{
    GpsService::getInstance().setCollectionInterval(interval_ms);
}

void gps_set_power_strategy(uint8_t strategy)
{
    GpsService::getInstance().setPowerStrategy(strategy);
}

void gps_set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    GpsService::getInstance().setGnssConfig(mode, sat_mask);
}

void gps_set_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    GpsService::getInstance().setNmeaConfig(output_hz, sentence_mask);
}

void gps_set_motion_idle_timeout(uint32_t timeout_ms)
{
    GpsService::getInstance().setMotionIdleTimeout(timeout_ms);
}

void gps_set_motion_sensor_id(uint8_t sensor_id)
{
    GpsService::getInstance().setMotionSensorId(sensor_id);
}

TaskHandle_t gps_get_task_handle()
{
    return GpsService::getInstance().getTaskHandle();
}

double calculate_map_resolution(int zoom, double lat)
{
    const double MAX_LAT = 85.05112878;
    double lat_clamped = lat;
    if (lat_clamped > MAX_LAT) lat_clamped = MAX_LAT;
    if (lat_clamped < -MAX_LAT) lat_clamped = -MAX_LAT;

    double resolution_equator = 156543.03392 / std::pow(2.0, zoom);

    double lat_rad = lat_clamped * M_PI / 180.0;
    double resolution = resolution_equator * std::cos(lat_rad);

    return resolution;
}

} // namespace gps
