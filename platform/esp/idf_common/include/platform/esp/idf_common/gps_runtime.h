#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps/domain/gnss_satellite.h"
#include "gps/domain/gps_state.h"

namespace platform::esp::idf_common::gps_runtime
{

gps::GpsState get_data();
bool get_gnss_snapshot(gps::GnssSatInfo* out, std::size_t max, std::size_t* out_count, gps::GnssStatus* status);
uint32_t last_motion_ms();
bool is_enabled();
bool is_powered();
void set_collection_interval(uint32_t interval_ms);
void set_power_strategy(uint8_t strategy);
void set_gnss_config(uint8_t mode, uint8_t sat_mask);
void set_nmea_config(uint8_t output_hz, uint8_t sentence_mask);
void set_motion_idle_timeout(uint32_t timeout_ms);
void set_motion_sensor_id(uint8_t sensor_id);
TaskHandle_t get_task_handle();
void request_startup_probe();

} // namespace platform::esp::idf_common::gps_runtime
