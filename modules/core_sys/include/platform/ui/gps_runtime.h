#pragma once

#include <cstddef>
#include <cstdint>

#include "gps/domain/gnss_satellite.h"
#include "gps/domain/gps_state.h"

namespace platform::ui::gps
{

using GpsState = ::gps::GpsState;
using GnssSatInfo = ::gps::GnssSatInfo;
using GnssStatus = ::gps::GnssStatus;
using GnssFix = ::gps::GnssFix;
using GnssSystem = ::gps::GnssSystem;

GpsState get_data();
bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status);
uint32_t last_motion_ms();
bool is_enabled();
bool is_powered();
void set_collection_interval(uint32_t interval_ms);
void set_power_strategy(uint8_t strategy);
void set_gnss_config(uint8_t mode, uint8_t sat_mask);
void set_nmea_config(uint8_t output_hz, uint8_t sentence_mask);
void set_motion_idle_timeout(uint32_t timeout_ms);
void set_motion_sensor_id(uint8_t sensor_id);
void suspend_runtime();
void resume_runtime();
double calculate_map_resolution(int zoom, double lat);

} // namespace platform::ui::gps
