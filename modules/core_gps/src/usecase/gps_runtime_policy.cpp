#include "gps/usecase/gps_runtime_policy.h"

namespace gps
{

uint32_t normalizeCollectionInterval(uint32_t interval_ms)
{
    if (interval_ms < kMinGpsCollectionIntervalMs)
    {
        return kMinGpsCollectionIntervalMs;
    }
    if (interval_ms > kMaxGpsCollectionIntervalMs)
    {
        return kMaxGpsCollectionIntervalMs;
    }
    return interval_ms;
}

uint32_t effectiveCollectionInterval(uint32_t interval_ms, int power_tier)
{
    uint32_t effective = normalizeCollectionInterval(interval_ms);

    if (power_tier >= 2 && effective < kCriticalPowerGpsCollectionFloorMs)
    {
        return kCriticalPowerGpsCollectionFloorMs;
    }
    if (power_tier >= 1 && effective < kLowPowerGpsCollectionFloorMs)
    {
        return kLowPowerGpsCollectionFloorMs;
    }
    return effective;
}

MotionConfig normalizeMotionConfig(const MotionConfig& config)
{
    MotionConfig normalized = config;
    if (normalized.idle_timeout_ms < kMinMotionIdleTimeoutMs)
    {
        normalized.idle_timeout_ms = kMinMotionIdleTimeoutMs;
    }
    return normalized;
}

GpsPowerDecision decideGpsPower(const GpsPowerInputs& inputs, uint32_t now_ms)
{
    GpsPowerDecision decision{};
    decision.motion_control_armed_ms = inputs.motion_control_armed_ms;

    if (inputs.power_strategy == 2)
    {
        decision.should_enable_gps = false;
        return decision;
    }

    if (inputs.team_mode_active)
    {
        decision.should_enable_gps = true;
        return decision;
    }

    if (inputs.motion_control_enabled && inputs.motion_policy_enabled)
    {
        uint32_t last_motion_ms = inputs.last_motion_ms;
        if (last_motion_ms == 0)
        {
            if (decision.motion_control_armed_ms == 0)
            {
                decision.motion_control_armed_ms = now_ms;
            }
            last_motion_ms = decision.motion_control_armed_ms;
        }

        decision.should_enable_gps =
            (now_ms - last_motion_ms) < inputs.motion_idle_timeout_ms;
        return decision;
    }

    decision.should_enable_gps = true;
    return decision;
}

} // namespace gps
