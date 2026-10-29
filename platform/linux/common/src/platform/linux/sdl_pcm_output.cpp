#include "platform/linux/sdl_pcm_output.h"
#include "platform/audio/audio_focus.h"
#include "sdl_audio_lifetime.h"
#include <SDL.h>
#include <cstdlib>

namespace platform::linux_runtime
{
SdlPcmOutput::SdlPcmOutput(bool music_focus, PcmActivity* activity, uint16_t buffer_samples)
    : music_focus_(music_focus), buffer_samples_(buffer_samples), activity_(activity ? activity : &owned_activity_)
{
    initialized_ = detail::acquireAudioSubsystem();
}
SdlPcmOutput::~SdlPcmOutput()
{
    end();
    if (initialized_) detail::releaseAudioSubsystem();
}
bool SdlPcmOutput::supported() const { return initialized_ && SDL_GetNumAudioDevices(0) > 0; }
bool SdlPcmOutput::blocked() const { return music_focus_ && platform::audio::music_blocked(); }
bool SdlPcmOutput::begin(uint32_t rate)
{
    return begin(rate, nullptr);
}
bool SdlPcmOutput::begin(uint32_t rate, const char* device_name)
{
    end();
    if (!initialized_ || rate < 8000 || rate > 48000 || blocked()) return false;
    SDL_AudioSpec desired{}, actual{};
    desired.freq = static_cast<int>(rate);
    desired.channels = 1;
    desired.format = AUDIO_S16SYS;
    desired.samples = buffer_samples_;
    activity_->opening = true;
    if (blocked())
    {
        activity_->opening = false;
        return false;
    }
    if (!device_name || !*device_name) device_name = std::getenv("TRAIL_MATE_AUDIO_OUTPUT_DEVICE");
    device_ = SDL_OpenAudioDevice(device_name && *device_name ? device_name : nullptr, 0, &desired, &actual, 0);
    if (!device_)
    {
        activity_->opening = false;
        failed_ = true;
        return false;
    }
    rate_ = rate;
    period_ms_ = static_cast<uint32_t>(actual.samples) * 1000 / rate;
    activity_->active = true;
    activity_->opening = false;
    failed_ = false;
    SDL_PauseAudioDevice(device_, 0);
    return true;
}
bool SdlPcmOutput::needs_frame() const { return pending_ms() < 1500; }
uint32_t SdlPcmOutput::pending_ms() const
{
    if (!device_ || !rate_) return 0;
    const auto bytes = SDL_GetQueuedAudioSize(device_);
    if (bytes) return uint64_t(bytes) * 1000 / (rate_ * sizeof(int16_t)) + period_ms_;
    if (drained_at_ == std::chrono::steady_clock::time_point{}) return period_ms_;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(drained_at_ - std::chrono::steady_clock::now()).count();
    return left > 0 ? static_cast<uint32_t>(left) : 0;
}
bool SdlPcmOutput::drain()
{
    if (!device_) return true;
    if (SDL_GetQueuedAudioSize(device_)) return false;
    if (drained_at_ == std::chrono::steady_clock::time_point{})
        drained_at_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(period_ms_ * 2);
    return std::chrono::steady_clock::now() >= drained_at_;
}
bool SdlPcmOutput::ready_for_optional_work() const { return pending_ms() >= 1000 || drained_at_ != std::chrono::steady_clock::time_point{}; }
bool SdlPcmOutput::write(const int16_t* samples, std::size_t count)
{
    if (!device_ || blocked() || !samples || !count || count > 1152 * 2) return false;
    drained_at_ = {};
    if (SDL_QueueAudio(device_, samples, static_cast<uint32_t>(count * sizeof(int16_t))) != 0)
    {
        failed_ = true;
        return false;
    }
    return true;
}
void SdlPcmOutput::discard_pending()
{
    if (device_) SDL_ClearQueuedAudio(device_);
    drained_at_ = {};
}
void SdlPcmOutput::end()
{
    if (device_)
    {
        SDL_ClearQueuedAudio(device_);
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    activity_->active = false;
    rate_ = 0;
    drained_at_ = {};
}
} // namespace platform::linux_runtime
