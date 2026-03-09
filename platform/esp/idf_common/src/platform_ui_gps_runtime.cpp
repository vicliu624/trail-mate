#include "platform/ui/gps_runtime.h"

#include "platform/esp/arduino_common/gps/gps_service_api.h"

namespace platform::ui::gps
{

GpsState get_data()
{
    return ::gps::gps_get_data();
}

bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status)
{
    return ::gps::gps_get_gnss_snapshot(out, max, out_count, status);
}

uint32_t last_motion_ms()
{
    return ::gps::gps_get_last_motion_ms();
}

bool is_enabled()
{
    return ::gps::gps_is_enabled();
}

bool is_powered()
{
    return ::gps::gps_is_powered();
}

void set_collection_interval(uint32_t interval_ms)
{
    ::gps::gps_set_collection_interval(interval_ms);
}

void set_power_strategy(uint8_t strategy)
{
    ::gps::gps_set_power_strategy(strategy);
}

void set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    ::gps::gps_set_gnss_config(mode, sat_mask);
}

void set_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    ::gps::gps_set_nmea_config(output_hz, sentence_mask);
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    ::gps::gps_set_motion_idle_timeout(timeout_ms);
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    ::gps::gps_set_motion_sensor_id(sensor_id);
}

void suspend_runtime()
{
    TaskHandle_t task_handle = ::gps::gps_get_task_handle();
    if (task_handle != nullptr)
    {
        vTaskSuspend(task_handle);
    }
}

void resume_runtime()
{
    TaskHandle_t task_handle = ::gps::gps_get_task_handle();
    if (task_handle != nullptr)
    {
        vTaskResume(task_handle);
    }
}

double calculate_map_resolution(int zoom, double lat)
{
    return ::gps::calculate_map_resolution(zoom, lat);
}

} // namespace platform::ui::gps
