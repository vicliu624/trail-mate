#include "platform/linux/sdl_pcm_input.h"
#include "sdl_audio_lifetime.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace platform::linux_runtime
{
struct SdlPcmInput::State
{
    std::array<int16_t, 48000> samples{};
    std::size_t limit = 0, head = 0, size = 0;
    uint64_t dropped = 0;
};
SdlPcmInput::SdlPcmInput() : initialized_(detail::acquireAudioSubsystem())
{
    if (!initialized_) error_ = SDL_GetError();
}
SdlPcmInput::~SdlPcmInput()
{
    end();
    if (initialized_) detail::releaseAudioSubsystem();
}
bool SdlPcmInput::available() const { return initialized_ && SDL_GetNumAudioDevices(1) > 0; }
bool SdlPcmInput::begin(uint32_t rate, const char* device_name)
{
    end();
    error_.clear();
    if (!initialized_ || rate < 8000 || rate > 48000)
    {
        error_ = "Audio input unavailable or sample rate outside 8-48 kHz";
        return false;
    }
    state_ = std::make_unique<State>();
    state_->limit = rate;
    SDL_AudioSpec desired{}, actual{};
    desired.freq = static_cast<int>(rate);
    desired.channels = 1;
    desired.format = AUDIO_S16SYS;
    desired.samples = 512;
    desired.callback = capture;
    desired.userdata = state_.get();
    if (!device_name || !*device_name) device_name = std::getenv("TRAIL_MATE_AUDIO_INPUT_DEVICE");
    device_ = SDL_OpenAudioDevice(device_name && *device_name ? device_name : nullptr,
                                  1, &desired, &actual, 0);
    if (!device_)
    {
        error_ = SDL_GetError();
        state_.reset();
        return false;
    }
    SDL_PauseAudioDevice(device_, 0);
    return true;
}
void SdlPcmInput::capture(void* context, uint8_t* stream, int bytes)
{
    auto& state = *static_cast<State*>(context);
    if (!stream || bytes <= 0) return;
    auto count = static_cast<std::size_t>(bytes) / sizeof(int16_t);
    if (count > state.limit)
    {
        state.dropped += count - state.limit;
        stream += (count - state.limit) * sizeof(int16_t);
        count = state.limit;
    }
    if (count > state.limit - state.size)
    {
        const auto discard = count - (state.limit - state.size);
        state.head = (state.head + discard) % state.limit;
        state.size -= discard;
        state.dropped += discard;
    }
    const auto tail = (state.head + state.size) % state.limit;
    const auto first = std::min(count, state.limit - tail);
    std::memcpy(state.samples.data() + tail, stream, first * sizeof(int16_t));
    std::memcpy(state.samples.data(), stream + first * sizeof(int16_t), (count - first) * sizeof(int16_t));
    state.size += count;
}
std::size_t SdlPcmInput::read(int16_t* output, std::size_t capacity)
{
    if (!device_ || !output || !capacity) return 0;
    SDL_LockAudioDevice(device_);
    const auto count = std::min(capacity, state_->size);
    const auto first = std::min(count, state_->limit - state_->head);
    std::memcpy(output, state_->samples.data() + state_->head, first * sizeof(int16_t));
    std::memcpy(output + first, state_->samples.data(), (count - first) * sizeof(int16_t));
    state_->head = (state_->head + count) % state_->limit;
    state_->size -= count;
    SDL_UnlockAudioDevice(device_);
    return count;
}
std::size_t SdlPcmInput::buffered_samples() const
{
    if (!device_) return 0;
    SDL_LockAudioDevice(device_);
    const auto count = state_->size;
    SDL_UnlockAudioDevice(device_);
    return count;
}
uint64_t SdlPcmInput::dropped_samples() const
{
    if (!device_) return 0;
    SDL_LockAudioDevice(device_);
    const auto count = state_->dropped;
    SDL_UnlockAudioDevice(device_);
    return count;
}
bool SdlPcmInput::active() const { return device_ && SDL_GetAudioDeviceStatus(device_) == SDL_AUDIO_PLAYING; }
void SdlPcmInput::discard_pending()
{
    if (!device_ || !state_) return;
    SDL_LockAudioDevice(device_);
    state_->head = state_->size = 0;
    SDL_UnlockAudioDevice(device_);
}
void SdlPcmInput::end()
{
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
    state_.reset();
}
} // namespace platform::linux_runtime
