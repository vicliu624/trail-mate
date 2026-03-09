#pragma once

#include "gps/domain/motion_config.h"
#include <cstdint>

namespace gps
{

constexpr uint32_t kMinGpsCollectionIntervalMs = 1000;
constexpr uint32_t kMaxGpsCollectionIntervalMs = 600000;
constexpr uint32_t kLowPowerGpsCollectionFloorMs = 30000;
constexpr uint32_t kCriticalPowerGpsCollectionFloorMs = 60000;
constexpr uint32_t kMinMotionIdleTimeoutMs = 5 * 60 * 1000;

struct GpsPowerInputs
{
    uint8_t power_strategy = 0;
    bool team_mode_active = false;
    bool motion_control_enabled = false;
    bool motion_policy_enabled = false;
    uint32_t motion_idle_timeout_ms = kMinMotionIdleTimeoutMs;
    uint32_t last_motion_ms = 0;
    uint32_t motion_control_armed_ms = 0;
};

struct GpsPowerDecision
{
    bool should_enable_gps = true;
    uint32_t motion_control_armed_ms = 0;
};

uint32_t normalizeCollectionInterval(uint32_t interval_ms);
uint32_t effectiveCollectionInterval(uint32_t interval_ms, int power_tier);
MotionConfig normalizeMotionConfig(const MotionConfig& config);
GpsPowerDecision decideGpsPower(const GpsPowerInputs& inputs, uint32_t now_ms);

} // namespace gps
