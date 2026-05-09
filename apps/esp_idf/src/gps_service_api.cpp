#include "platform/esp/arduino_common/gps/gps_service_api.h"

#include <cmath>

#include "platform/esp/idf_common/gps_runtime.h"

namespace gps
{

GpsState gps_get_data()
{
    return platform::esp::idf_common::gps_runtime::get_data();
}

bool gps_get_gnss_snapshot(gps::GnssSatInfo* out, size_t max, size_t* out_count, gps::GnssStatus* status)
{
    return platform::esp::idf_common::gps_runtime::get_gnss_snapshot(out, max, out_count, status);
}

uint32_t gps_get_last_motion_ms()
{
    return platform::esp::idf_common::gps_runtime::last_motion_ms();
}

bool gps_is_enabled()
{
    return platform::esp::idf_common::gps_runtime::is_enabled();
}

bool gps_is_powered()
{
    return platform::esp::idf_common::gps_runtime::is_powered();
}

void gps_set_enabled(bool enabled)
{
    platform::esp::idf_common::gps_runtime::set_enabled(enabled);
}

void gps_set_collection_interval(uint32_t interval_ms)
{
    platform::esp::idf_common::gps_runtime::set_collection_interval(interval_ms);
}

void gps_set_power_strategy(uint8_t strategy)
{
    platform::esp::idf_common::gps_runtime::set_power_strategy(strategy);
}

void gps_set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    platform::esp::idf_common::gps_runtime::set_gnss_config(mode, sat_mask);
}

void gps_set_external_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    platform::esp::idf_common::gps_runtime::set_external_nmea_config(output_hz, sentence_mask);
}

void gps_set_motion_idle_timeout(uint32_t timeout_ms)
{
    platform::esp::idf_common::gps_runtime::set_motion_idle_timeout(timeout_ms);
}

void gps_set_motion_sensor_id(uint8_t sensor_id)
{
    platform::esp::idf_common::gps_runtime::set_motion_sensor_id(sensor_id);
}

TaskHandle_t gps_get_task_handle()
{
    return platform::esp::idf_common::gps_runtime::get_task_handle();
}

double calculate_map_resolution(int zoom, double lat)
{
    const double max_lat = 85.05112878;
    double lat_clamped = lat;
    if (lat_clamped > max_lat) lat_clamped = max_lat;
    if (lat_clamped < -max_lat) lat_clamped = -max_lat;

    const double resolution_equator = 156543.03392 / std::pow(2.0, zoom);
    const double lat_rad = lat_clamped * M_PI / 180.0;
    return resolution_equator * std::cos(lat_rad);
}

} // namespace gps
