/**
 * @file time_utils.h
 * @brief Time helpers for chat timestamps
 */

#pragma once

#include "sys/clock.h"
#include <cstdint>

namespace chat
{

constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01

inline bool is_valid_epoch(uint32_t ts)
{
    return ts >= kMinValidEpochSeconds;
}

inline uint32_t now_epoch_seconds()
{
    return sys::epoch_seconds_now();
}

inline uint32_t now_uptime_seconds()
{
    return sys::uptime_seconds_now();
}

inline uint32_t now_message_timestamp()
{
    uint32_t now = now_epoch_seconds();
    return is_valid_epoch(now) ? now : now_uptime_seconds();
}

} // namespace chat
