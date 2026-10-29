#pragma once

#include <atomic>
#include <chrono>
#include <music/player.h>

namespace platform::linux_runtime
{
struct PcmActivity
{
    std::atomic_bool active{false}, opening{false};
};
class SdlPcmOutput final : public ::music::AudioOutput
{
  public:
    explicit SdlPcmOutput(bool music_focus = true, PcmActivity* activity = nullptr, uint16_t buffer_samples = 1024);
    ~SdlPcmOutput() override;
    bool supported() const override;
    bool blocked() const override;
    bool needs_frame() const override;
    bool failed() const override { return failed_; }
    uint32_t pending_ms() const override;
    bool drain() override;
    bool ready_for_optional_work() const override;
    bool begin(uint32_t rate) override;
    bool begin(uint32_t rate, const char* device_name);
    bool write(const int16_t* samples, std::size_t count) override;
    void end() override;
    // Worker-only mute/mode transition: discard queued audio without reopening.
    void discard_pending();
    bool active() const { return activity_->active.load(); }
    bool opening() const { return activity_->opening.load(); }
    uint32_t period_ms() const { return period_ms_; }

  private:
    bool initialized_ = false, music_focus_ = true, failed_ = false;
    uint32_t device_ = 0, rate_ = 0, period_ms_ = 0;
    uint16_t buffer_samples_;
    PcmActivity owned_activity_;
    PcmActivity* activity_;
    std::chrono::steady_clock::time_point drained_at_{};
};
} // namespace platform::linux_runtime
