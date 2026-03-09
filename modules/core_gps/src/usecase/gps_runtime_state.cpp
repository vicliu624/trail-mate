#include "gps/usecase/gps_runtime_state.h"

namespace gps
{

uint32_t GpsRuntimeState::collectionIntervalMs(int power_tier) const
{
    return effectiveCollectionInterval(requested_collection_interval_ms_, power_tier);
}

void GpsRuntimeState::setCollectionInterval(uint32_t interval_ms)
{
    requested_collection_interval_ms_ = normalizeCollectionInterval(interval_ms);
}

bool GpsRuntimeState::setTeamModeActive(bool active)
{
    if (team_mode_active_ == active)
    {
        return false;
    }

    team_mode_active_ = active;
    return true;
}

void GpsRuntimeState::setMotionControlEnabled(bool enabled, uint32_t now_ms)
{
    motion_control_enabled_ = enabled;
    motion_control_armed_ms_ = enabled ? now_ms : 0;
}

GpsPowerInputs GpsRuntimeState::makePowerInputs(bool motion_policy_enabled,
                                                uint32_t motion_idle_timeout_ms,
                                                uint32_t last_motion_ms) const
{
    GpsPowerInputs inputs{};
    inputs.power_strategy = power_strategy_;
    inputs.team_mode_active = team_mode_active_;
    inputs.motion_control_enabled = motion_control_enabled_;
    inputs.motion_policy_enabled = motion_policy_enabled;
    inputs.motion_idle_timeout_ms = motion_idle_timeout_ms;
    inputs.last_motion_ms = last_motion_ms;
    inputs.motion_control_armed_ms = motion_control_armed_ms_;
    return inputs;
}

} // namespace gps
