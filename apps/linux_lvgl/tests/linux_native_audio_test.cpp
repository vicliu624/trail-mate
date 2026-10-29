#include "platform/audio/audio_focus.h"
#include "platform/audio/media_volume.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/sdl_pcm_input.h"
#include "platform/ui/device_runtime.h"
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <thread>

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}
void waitFor(const std::function<bool()>& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("audio condition timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
std::atomic<unsigned> suspends{0}, resumes{0};
bool suspend(void*)
{
    ++suspends;
    return true;
}
void resume(void*) { ++resumes; }
} // namespace

int main()
{
    using namespace platform::linux_runtime;
    require(!native_audio_snapshot().initialized, "snapshot started audio I/O");
    require(!queue_message_tone(0), "muted tone was queued");
    require(!native_audio_snapshot().initialized, "muted request started audio I/O");
    prepare_native_audio();
    waitFor([]
            { return native_audio_snapshot().initialized; });
    require(native_audio_snapshot().playback_available, "dummy playback not found");
    require(native_audio_snapshot().capture_available, "dummy input not found");

    platform::audio::register_music_focus_client({nullptr, suspend, resume});
    platform::ui::device::set_message_tone_volume(255);
    require(platform::ui::device::default_message_tone_volume() == 100, "tone volume was not clamped");
    require(platform::audio::media_volume(0) == 100, "speaker setting did not update media gain");
    platform::ui::device::play_message_tone();
    waitFor([]
            { return native_audio_snapshot().tone_playing; });
    require(platform::audio::music_blocked(), "tone did not reserve focus");
    waitFor([]
            { return native_audio_snapshot().completed_tones == 1; });
    require(!platform::audio::music_blocked(), "tone leaked focus");
    require(suspends == 1 && resumes == 1, "music suspension/resume was not balanced");
    platform::audio::publish_media_volume(0);
    require(platform::ui::device::default_message_tone_volume() == 0, "media mute did not apply to alerts");
    platform::ui::device::play_message_tone();
    require(!native_audio_snapshot().tone_playing, "muted preview started playback");
    {
        platform::audio::FocusScope call(platform::audio::FocusOwner::Call);
        require(bool(call), "call focus failed");
        require(queue_message_tone(45), "tone intent rejected");
        waitFor([]
                { return native_audio_snapshot().skipped_tones == 1; });
        require(native_audio_snapshot().completed_tones == 1, "skipped tone was counted as playback");
    }
    platform::audio::register_music_focus_client({});

    SdlPcmInput input;
    require(input.available(), "capture unavailable");
    require(!input.begin(0), "invalid capture rate accepted");
    require(input.begin(8000), input.last_error());
    require(input.active(), "capture did not start");
    waitFor([&]
            { return input.buffered_samples() >= 512; });
    std::array<int16_t, 512> samples;
    require(input.read(samples.data(), samples.size()) == samples.size(), "capture returned no PCM");
    for (auto sample : samples) require(sample == 0, "dummy capture should contain silence");
    require(input.read(nullptr, 100) == 0, "null destination accepted");
    waitFor([&]
            { return input.dropped_samples() > 0; });
    require(input.buffered_samples() <= 8000, "capture ring exceeded one second");
    input.end();
    require(!input.active() && input.buffered_samples() == 0, "capture stop retained data/device");
    require(!input.begin(16000, "trailmate-nonexistent-input"), "missing input was accepted");
    require(!input.active(), "failed capture retained a device");
    require(input.begin(16000), "capture failed to recover");
    input.end();

    require(queue_message_tone(30), "shutdown tone not queued");
    shutdown_native_audio();
    require(!native_audio_snapshot().tone_playing, "shutdown left playback active");
    require(!platform::audio::music_blocked(), "shutdown leaked focus");
    require(!queue_message_tone(30), "stopped service accepted new work");
    return 0;
}
