#pragma once

#include "domain/gps_state.h"
#include "domain/motion_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace gps
{

GpsState gps_get_data();
void gps_set_collection_interval(uint32_t interval_ms);
void gps_set_motion_idle_timeout(uint32_t timeout_ms);
void gps_set_motion_sensor_id(uint8_t sensor_id);
TaskHandle_t gps_get_task_handle();

// Calculate map resolution (meters per pixel) at given zoom and latitude
double calculate_map_resolution(int zoom, double lat);

} // namespace gps
