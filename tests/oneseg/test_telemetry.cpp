#include "platform/linux/oneseg_telemetry.h"
#include <cassert>
#include <cstring>
#include <memory>

using namespace platform::linux_runtime;
int main()
{
    auto state = std::make_unique<OneSegTvSession::Snapshot>();
    auto& s = *state;
    s.active = true;
    s.generation = 7;
    s.media_epoch = s.media.generation = 9;
    s.media.video_frames = 100;
    s.media.pcm_samples = 48000;
    s.iq_age_ms = s.video_age_ms = 20;
    s.timing = OneSegTvSession::Timing::AudioQueueEstimate;
    s.receive.forwarded_pairs = UINT64_MAX;
    OneSegAudioClock::Snapshot audio;
    audio.generation = 9;
    audio.valid = true;
    audio.queued_us = 100000;
    char buffer[2048];
    auto emit = [&](unsigned age)
    {
        const int count = format_oneseg_telemetry(buffer, sizeof(buffer), s, 123456, age, audio, 42);
        assert(count > 0 && size_t(count) == std::strlen(buffer));
        std::fputs(buffer, stdout);
    };
    emit(20); // Decoded video must not be reported as presented video.
    s.presented_frames = 50;
    s.phase = OneSegTvSession::Phase::FramePresented;
    emit(20);
    emit(2001); // Last displayed frame has become stale.
    audio.starved = true;
    audio.valid = false;
    emit(20); // PCM decode counters alone must not attest active audio output.
    audio.starved = false;
    audio.valid = true;
    s.phase = OneSegTvSession::Phase::Stopping;
    emit(20); // A stopped presentation must not be reclassified by fresh counters.
    char tiny[8];
    assert(format_oneseg_telemetry(tiny, sizeof(tiny), s, 1, 0, audio) == 0);
}
