/**
 * @file team_track_sampler.h
 * @brief Periodic Team track sampler (fixed interval, max 5 points)
 */

#pragma once

#include "../protocol/team_track.h"
#include "../usecase/team_controller.h"
#include <array>

namespace team
{

class TeamTrackSampler
{
  public:
    void update(TeamController* controller, bool team_active);
    void reset();

  private:
    void resetWindow(bool keep_schedule);
    void startWindow(uint32_t now_ms);
    void samplePoint(uint32_t now_ms);
    void flushWindow(TeamController* controller);

    static constexpr uint32_t kSampleIntervalMs = 120000;

    uint32_t next_sample_ms_ = 0;
    uint32_t start_ts_ = 0;
    uint8_t count_ = 0;
    uint32_t valid_mask_ = 0;
    std::array<proto::TeamTrackPoint, proto::kTeamTrackMaxPoints> points_{};
};

} // namespace team
