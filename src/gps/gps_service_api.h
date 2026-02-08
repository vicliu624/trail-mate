#pragma once

#include "domain/gnss_satellite.h"
#include "domain/gps_state.h"
#include "domain/motion_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace gps
{

GpsState gps_get_data();
bool gps_get_gnss_snapshot(gps::GnssSatInfo* out, size_t max, size_t* out_count, gps::GnssStatus* status);
uint32_t gps_get_last_motion_ms();
void gps_set_collection_interval(uint32_t interval_ms);
void gps_set_power_strategy(uint8_t strategy);
void gps_set_gnss_config(uint8_t mode, uint8_t sat_mask);
void gps_set_nmea_config(uint8_t output_hz, uint8_t sentence_mask);
void gps_set_motion_idle_timeout(uint32_t timeout_ms);
void gps_set_motion_sensor_id(uint8_t sensor_id);
TaskHandle_t gps_get_task_handle();

// Calculate map resolution (meters per pixel) at given zoom and latitude
double calculate_map_resolution(int zoom, double lat);

} // namespace gps
