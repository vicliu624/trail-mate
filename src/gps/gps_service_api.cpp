#include "gps_service_api.h"
#include "usecase/gps_service.h"
#include <cmath>

namespace gps {

GpsState gps_get_data()
{
    return GpsService::getInstance().getData();
}

void gps_set_collection_interval(uint32_t interval_ms)
{
    GpsService::getInstance().setCollectionInterval(interval_ms);
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

}  // namespace gps
