#pragma once

#include "gps_runtime_policy.h"
#include <cstdint>

namespace gps
{

constexpr uint32_t kDefaultGpsCollectionIntervalMs = 60000;

class GpsRuntimeState
{
  public:
    uint32_t requestedCollectionIntervalMs() const { return requested_collection_interval_ms_; }
    uint32_t collectionIntervalMs(int power_tier) const;
    void setCollectionInterval(uint32_t interval_ms);

    uint8_t powerStrategy() const { return power_strategy_; }
    void setPowerStrategy(uint8_t strategy) { power_strategy_ = strategy; }

    bool teamModeActive() const { return team_mode_active_; }
    bool setTeamModeActive(bool active);

    bool motionControlEnabled() const { return motion_control_enabled_; }
    void setMotionControlEnabled(bool enabled, uint32_t now_ms);

    uint32_t motionControlArmedMs() const { return motion_control_armed_ms_; }
    void setMotionControlArmedMs(uint32_t armed_ms) { motion_control_armed_ms_ = armed_ms; }

    GpsPowerInputs makePowerInputs(bool motion_policy_enabled, uint32_t motion_idle_timeout_ms,
                                   uint32_t last_motion_ms) const;

  private:
    uint32_t requested_collection_interval_ms_ = kDefaultGpsCollectionIntervalMs;
    uint8_t power_strategy_ = 0;
    bool team_mode_active_ = false;
    bool motion_control_enabled_ = false;
    uint32_t motion_control_armed_ms_ = 0;
};

} // namespace gps
