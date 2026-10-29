#include "platform/linux/native_audio_service.h"

#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
#include "platform/audio/audio_focus.h"
#include "platform/linux/sdl_pcm_input.h"
#include "platform/linux/sdl_pcm_output.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace platform::linux_runtime
{
namespace
{
class NativeAudio;
std::atomic<NativeAudio*> existing{nullptr};
class NativeAudio
{
  public:
    NativeAudio() { existing = this; }
    ~NativeAudio() { shutdown(); }
    NativeAudioSnapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return published_;
    }
    void prepare()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        (void)startLocked();
    }
    bool queue(uint8_t volume)
    {
        if (!volume) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        // A single pending chime coalesces bursts rather than accumulating lag.
        pending_volume_ = std::min<unsigned>(volume, 100);
        if (!startLocked())
        {
            pending_volume_ = 0;
            return false;
        }
        wake_.notify_all();
        return true;
    }
    void shutdown()
    {
        stopping_ = true;
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        pending_volume_ = 0;
        published_.tone_playing = false;
    }

  private:
    enum class Outcome
    {
        Played,
        Failed,
        Skipped
    };
    bool startLocked()
    {
        if (stopping_) return false;
        if (!worker_.joinable())
        {
            try
            {
                worker_ = std::thread([this]
                                      { run(); });
            }
            catch (...)
            {
                ++published_.failed_tones;
                return false;
            }
        }
        return true;
    }
    Outcome play(SdlPcmOutput& output, uint8_t volume)
    {
        // Do not cover an already active call, voice message or RF audio owner.
        if (platform::audio::music_blocked())
        {
            return Outcome::Skipped;
        }
        platform::audio::FocusScope focus(platform::audio::FocusOwner::Notification, true);
        if (!focus || stopping_ || !output.begin(16000)) return Outcome::Failed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            published_.tone_playing = true;
        }
        std::array<int16_t, 512> samples;
        constexpr unsigned total = 3840; // Two 100 ms notes with a 40 ms gap.
        bool success = true;
        for (unsigned offset = 0; offset < total && !stopping_;)
        {
            const auto count = std::min<unsigned>(samples.size(), total - offset);
            for (unsigned index = 0; index < count; ++index)
            {
                const auto frame = offset + index;
                const bool second = frame >= 2240;
                const auto local = second ? frame - 2240 : frame;
                const auto envelope = local < 1600 ? std::min<unsigned>({local, 1599 - local, 80}) / 80.0 : 0.0;
                const auto frequency = second ? 1174.66 : 880.0;
                samples[index] = static_cast<int16_t>(std::sin(local * frequency * 6.283185307179586 / 16000) * envelope * volume * 120.0);
            }
            if (!output.write(samples.data(), count))
            {
                success = false;
                break;
            }
            offset += count;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (success && !stopping_ && !output.drain())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                success = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        output.end();
        return success && !stopping_ ? Outcome::Played : Outcome::Failed;
    }
    void run()
    {
        // std::thread does not run SDL's thread wrapper cleanup. Keep this
        // guard before the PCM owners so their destructors finish first.
        struct ThreadStorage
        {
            ~ThreadStorage() { SDL_TLSCleanup(); }
        } thread_storage;
        SdlPcmOutput output(false);
        SdlPcmInput input; // Enumerates input devices only; no recording begins.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            published_.initialized = true;
            published_.playback_available = output.supported();
            published_.capture_available = input.available();
        }
        while (!stopping_)
        {
            uint8_t volume;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!wake_.wait_for(lock, std::chrono::seconds(1), [this]
                                    { return stopping_ || pending_volume_; }))
                {
                    published_.playback_available = output.supported();
                    published_.capture_available = input.available();
                    continue;
                }
                if (stopping_) break;
                volume = pending_volume_;
                pending_volume_ = 0;
            }
            const auto outcome = play(output, volume);
            std::lock_guard<std::mutex> lock(mutex_);
            published_.tone_playing = false;
            if (outcome == Outcome::Played) ++published_.completed_tones;
            else if (outcome == Outcome::Failed) ++published_.failed_tones;
            else ++published_.skipped_tones;
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
    std::atomic_bool stopping_{false};
    uint8_t pending_volume_ = 0;
    NativeAudioSnapshot published_;
};
NativeAudio& service()
{
    static NativeAudio value;
    return value;
}
} // namespace
NativeAudioSnapshot native_audio_snapshot() { return service().snapshot(); }
void prepare_native_audio() { service().prepare(); }
bool queue_message_tone(uint8_t volume) { return service().queue(volume); }
void shutdown_native_audio()
{
    if (auto* value = existing.load()) value->shutdown();
}
} // namespace platform::linux_runtime
#else
namespace platform::linux_runtime
{
NativeAudioSnapshot native_audio_snapshot() { return {}; }
void prepare_native_audio() {}
bool queue_message_tone(uint8_t) { return false; }
void shutdown_native_audio() {}
} // namespace platform::linux_runtime
#endif
