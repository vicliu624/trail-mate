#pragma once
#include "platform/linux/call_audio_service.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>

namespace call_audio_test
{
struct State
{
    std::atomic<unsigned> live{0}, opened{0}, closed{0}, written{0}, discarded{0};
    std::atomic<bool> active{true}, fail_open{false}, block_open{false}, entered{false}, release_open{false};
    std::mutex mutex;
    std::condition_variable changed;
    void release()
    {
        release_open = true;
        changed.notify_all();
    }
};
class Pcm final : public platform::linux_runtime::ICallPcm
{
  public:
    explicit Pcm(std::shared_ptr<State> state) : state_(std::move(state)) {}
    ~Pcm() override { close(); }
    bool open(uint32_t rate) override
    {
        if (rate != 8000) return false;
        opened_ = true;
        ++state_->live;
        ++state_->opened;
        state_->entered = true;
        state_->changed.notify_all();
        if (state_->block_open)
        {
            std::unique_lock<std::mutex> lock(state_->mutex);
            state_->changed.wait(lock, [&]
                                 { return state_->release_open.load(); });
        }
        next_ = std::chrono::steady_clock::now();
        return !state_->fail_open;
    }
    bool captureActive() const override { return opened_ && state_->active; }
    uint64_t captureDropped() const override { return 0; }
    size_t read(int16_t* output, size_t count) override
    {
        if (std::chrono::steady_clock::now() < next_) return 0;
        count = std::min<size_t>(count, 160);
        for (size_t index = 0; index < count; ++index) output[index] = int16_t(4000 * std::sin(double(position_++) * .2));
        next_ += std::chrono::microseconds(count * 1000000 / 8000);
        return count;
    }
    bool writable() const override { return opened_; }
    void discardCapture() override { next_ = std::chrono::steady_clock::now(); }
    bool write(const int16_t*, size_t) override
    {
        ++state_->written;
        return true;
    }
    void discardPlayback() override { ++state_->discarded; }
    void close() noexcept override
    {
        if (!opened_) return;
        opened_ = false;
        --state_->live;
        ++state_->closed;
    }

  private:
    std::shared_ptr<State> state_;
    bool opened_ = false;
    size_t position_ = 0;
    std::chrono::steady_clock::time_point next_;
};
inline platform::linux_runtime::CallAudioOptions options(std::shared_ptr<State> state)
{
    platform::linux_runtime::CallAudioOptions value;
    value.available = []
    { return true; };
    value.make_pcm = [state]
    { return std::make_unique<Pcm>(state); };
    value.ring_alert = false;
    return value;
}
} // namespace call_audio_test
