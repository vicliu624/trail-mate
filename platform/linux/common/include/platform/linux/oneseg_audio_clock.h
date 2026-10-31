#pragma once
#include <algorithm>
#include <cstdint>
#include <mutex>

namespace platform::linux_runtime
{
// Queue-based estimate, not a hardware DAC timestamp. Updates are worker-only;
// UI reads use try_lock and never query SDL or wait for device cleanup.
class OneSegAudioClock
{
  public:
    struct Snapshot
    {
        uint64_t generation = 0;
        int64_t pts_us = INT64_MIN;
        uint32_t queued_us = 0;
        bool valid = false, starved = false;
    };
    static bool timestampValid(int64_t pts) { return pts >= -INT64_MAX / 4 && pts <= INT64_MAX / 4; }
    void reset(uint64_t generation)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = {};
        state_.generation = generation;
        sampled_us_ = 0;
        end_pts_ = INT64_MIN;
    }
    void submit(uint64_t generation, int64_t end_pts, uint32_t queued_us, int64_t now_us)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!generation || generation != state_.generation) return;
        state_.valid = timestampValid(end_pts) && queued_us > 0 && queued_us <= 1000000 && now_us >= 0;
        state_.starved = false;
        state_.queued_us = queued_us;
        end_pts_ = state_.valid ? end_pts : INT64_MIN;
        sampled_us_ = now_us;
    }
    Snapshot snapshot(int64_t now_us) const
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock) return {};
        auto result = state_;
        if (!result.valid || now_us < sampled_us_)
        {
            result.valid = false;
            return result;
        }
        const int64_t elapsed = now_us - sampled_us_;
        const auto remaining = std::max<int64_t>(0, int64_t(result.queued_us) - elapsed);
        result.pts_us = end_pts_ - remaining;
        result.queued_us = static_cast<uint32_t>(remaining);
        result.starved = !remaining;
        result.valid = !result.starved;
        return result;
    }

  private:
    mutable std::mutex mutex_;
    Snapshot state_;
    int64_t sampled_us_ = 0, end_pts_ = INT64_MIN;
};
} // namespace platform::linux_runtime
