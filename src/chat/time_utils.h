/**
 * @file time_utils.h
 * @brief Time helpers for chat timestamps
 */

#pragma once

#include <Arduino.h>
#include <cstdint>
#include <ctime>

namespace chat {

constexpr uint32_t kMinValidEpochSeconds = 1577836800U; // 2020-01-01

inline bool is_valid_epoch(uint32_t ts)
{
    return ts >= kMinValidEpochSeconds;
}

inline uint32_t now_epoch_seconds()
{
    time_t now = time(nullptr);
    if (now < 0) {
        return 0;
    }
    return static_cast<uint32_t>(now);
}

inline uint32_t now_uptime_seconds()
{
    return static_cast<uint32_t>(millis() / 1000U);
}

inline uint32_t now_message_timestamp()
{
    uint32_t now = now_epoch_seconds();
    return is_valid_epoch(now) ? now : now_uptime_seconds();
}

} // namespace chat
