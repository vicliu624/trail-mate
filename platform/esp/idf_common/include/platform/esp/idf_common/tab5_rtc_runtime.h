#pragma once

#include <cstdint>
#include <ctime>

namespace platform::esp::idf_common::tab5_rtc_runtime
{

constexpr std::time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 00:00:00 UTC

bool is_valid_epoch(std::time_t epoch_seconds);
bool validate_datetime_utc(int year,
                           uint8_t month,
                           uint8_t day,
                           uint8_t hour,
                           uint8_t minute,
                           uint8_t second);
std::time_t datetime_to_epoch_utc(int year,
                                  uint8_t month,
                                  uint8_t day,
                                  uint8_t hour,
                                  uint8_t minute,
                                  uint8_t second);

bool apply_system_time(std::time_t epoch_seconds, const char* source);
bool apply_system_time_and_sync_rtc(std::time_t epoch_seconds, const char* source);
bool sync_system_time_from_hardware_rtc();
bool sync_hardware_rtc_from_system_time();

} // namespace platform::esp::idf_common::tab5_rtc_runtime
