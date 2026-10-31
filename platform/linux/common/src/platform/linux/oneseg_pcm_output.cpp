#include "platform/linux/oneseg_pcm_output.h"
#include "platform/audio/audio_focus.h"
#include "platform/linux/sdl_pcm_output.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

namespace platform::linux_runtime
{
struct OneSegPcmOutput::Impl
{
    std::unique_ptr<SdlPcmOutput> output;
    platform::audio::FocusToken focus;
    std::atomic<unsigned> volume{70};
    std::array<int16_t, 2048> samples;
    uint64_t generation = 0;
    OneSegAudioClock clock;
    int64_t next_pts = INT64_MIN;
    bool written = false;
    static int64_t nowUs()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    char error[128]{};
    bool fail(const char* message)
    {
        if (!error[0]) std::snprintf(error, sizeof(error), "%s", message);
        return false;
    }
};
OneSegPcmOutput::OneSegPcmOutput() : impl_(std::make_unique<Impl>()) {}
OneSegPcmOutput::~OneSegPcmOutput() { (void)end(true); }
bool OneSegPcmOutput::begin(uint64_t generation)
{
    auto& s = *impl_;
    if (s.output || !generation) return false;
    s.error[0] = 0;
    s.focus = platform::audio::request_exclusive_focus(platform::audio::FocusOwner::Television);
    if (!s.focus) return s.fail("Television audio focus unavailable");
    try
    {
        s.output = std::make_unique<SdlPcmOutput>(false, nullptr, 512);
    }
    catch (...)
    {
        (void)end(true);
        throw;
    }
    if (!s.output->begin(48000))
    {
        s.fail("Cannot open television PCM output at 48 kHz");
        (void)end(true);
        return false;
    }
    s.generation = generation;
    s.next_pts = INT64_MIN;
    s.written = false;
    s.clock.reset(generation);
    return true;
}
bool OneSegPcmOutput::write(const int16_t* input, size_t count, int64_t pts, uint64_t generation,
                            const std::atomic_bool& cancel)
{
    auto& s = *impl_;
    if (!s.output || !input || !count || count > s.samples.size() || generation != s.generation)
        return s.fail("Invalid or stale television PCM");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!cancel.load() && s.output->pending_ms() >= 150)
    {
        if (s.output->failed() || std::chrono::steady_clock::now() >= deadline)
            return s.fail("Television PCM consumer stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (cancel.load()) return false;
    const bool timestamped = OneSegAudioClock::timestampValid(pts);
    if (timestamped && s.written &&
        (s.next_pts == INT64_MIN || pts - s.next_pts > 100000 || pts - s.next_pts < -100000))
        s.output->discard_pending();
    const unsigned volume = s.volume.load();
    for (size_t i = 0; i < count; ++i) s.samples[i] = static_cast<int16_t>(int32_t(input[i]) * volume / 100);
    if (!s.output->write(s.samples.data(), count)) return s.fail("Television PCM write failed");
    s.next_pts = timestamped ? pts + static_cast<int64_t>(count * 1000000 / 48000) : INT64_MIN;
    s.written = true;
    s.clock.submit(generation, s.next_pts, s.output->pending_ms() * 1000, Impl::nowUs());
    return true;
}
bool OneSegPcmOutput::end(bool cancelled)
{
    auto& s = *impl_;
    s.clock.reset(0);
    if (s.output)
    {
        if (!cancelled)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
            while (!s.output->drain() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (!s.output->drain()) s.fail("Television PCM drain timed out");
        }
        s.output.reset(); // Close the physical device before resuming music.
    }
    s.generation = 0;
    platform::audio::release_focus(s.focus);
    s.focus = {};
    return !s.error[0];
}
const char* OneSegPcmOutput::lastError() const { return impl_->error; }
void OneSegPcmOutput::setVolume(unsigned percent) { impl_->volume.store(std::min(percent, 100u)); }
OneSegAudioClock::Snapshot OneSegPcmOutput::clock() const { return impl_->clock.snapshot(Impl::nowUs()); }
} // namespace platform::linux_runtime
