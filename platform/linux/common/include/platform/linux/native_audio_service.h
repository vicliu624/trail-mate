#pragma once
#include <cstdint>

namespace platform::linux_runtime
{
struct NativeAudioSnapshot
{
    bool initialized = false;
    bool playback_available = false;
    bool capture_available = false;
    bool tone_playing = false;
    uint32_t completed_tones = 0, failed_tones = 0, skipped_tones = 0;
};
// These getters/producers do not open an audio device. Preparation and queued
// work run on a worker; the application joins it before music/SDL teardown.
NativeAudioSnapshot native_audio_snapshot();
void prepare_native_audio();
bool queue_message_tone(uint8_t volume);
void shutdown_native_audio();
} // namespace platform::linux_runtime
