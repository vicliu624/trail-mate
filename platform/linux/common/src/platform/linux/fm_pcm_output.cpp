#include "platform/linux/fm_pcm_output.h"
#include "platform/audio/audio_focus.h"
#include "platform/linux/sdl_pcm_output.h"
#include "platform/shared/radio/fm_iq_consumer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <thread>

namespace platform::linux_runtime
{
struct FmPcmOutput::Impl
{
    std::unique_ptr<SdlPcmOutput> output;
    platform::audio::FocusToken focus;
    std::atomic<unsigned> volume{50};
    std::array<int16_t, platform::shared::radio::FmIqConsumer::kMaxPcmSamples> samples{};
    uint64_t session = 0;
    char error[128]{};
    bool fail(const char* message)
    {
        if (!error[0]) std::snprintf(error, sizeof(error), "%s", message);
        return false;
    }
};
FmPcmOutput::FmPcmOutput() : impl_(std::make_unique<Impl>()) {}
FmPcmOutput::~FmPcmOutput() { end(); }
bool FmPcmOutput::begin(uint64_t session)
{
    auto& s = *impl_;
    if (s.output || s.focus || !session) return false;
    s.error[0] = 0;
    s.focus = platform::audio::request_exclusive_focus(platform::audio::FocusOwner::FmRadio);
    if (!s.focus) return s.fail("FM audio focus unavailable");
    try
    {
        s.output = std::make_unique<SdlPcmOutput>(false, nullptr, 512);
        if (!s.output->begin(48000))
        {
            s.fail("Cannot open FM PCM output at 48 kHz");
            end();
            return false;
        }
    }
    catch (...)
    {
        end();
        throw;
    }
    s.session = session;
    return true;
}
bool FmPcmOutput::write(const int16_t* input, size_t count, uint64_t session,
                        const std::atomic_bool& cancel)
{
    auto& s = *impl_;
    if (!s.output || !input || !count || count > s.samples.size() || session != s.session)
        return s.fail("Invalid or stale FM PCM");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!cancel.load(std::memory_order_acquire) && s.output->pending_ms() >= 80)
    {
        if (s.output->failed() || std::chrono::steady_clock::now() >= deadline)
            return s.fail("FM PCM output stalled");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (cancel.load(std::memory_order_acquire)) return false;
    const unsigned volume = s.volume.load(std::memory_order_relaxed);
    for (size_t i = 0; i < count; ++i)
        s.samples[i] = static_cast<int16_t>(int32_t(input[i]) * volume / 100);
    if (!s.output->write(s.samples.data(), count)) return s.fail("FM PCM write failed");
    return true;
}
void FmPcmOutput::end() noexcept
{
    auto& s = *impl_;
    // Closing discards queued station audio before another owner can reopen.
    s.output.reset();
    s.session = 0;
    platform::audio::release_focus(s.focus);
    s.focus = {};
}
void FmPcmOutput::setVolume(unsigned percent) { impl_->volume.store(std::min(percent, 100u), std::memory_order_relaxed); }
const char* FmPcmOutput::lastError() const { return impl_->error; }
} // namespace platform::linux_runtime
