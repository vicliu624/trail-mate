#include "platform/audio/audio_focus.h"
#include "platform/linux/oneseg_pcm_output.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>

using namespace platform::audio;
struct Music
{
    unsigned suspended = 0, resumed = 0;
    bool ack = true;
    static bool suspend(void* p)
    {
        auto& s = *static_cast<Music*>(p);
        ++s.suspended;
        assert(music_blocked());
        return s.ack;
    }
    static void resume(void* p)
    {
        auto& s = *static_cast<Music*>(p);
        ++s.resumed;
        assert(!music_blocked());
    }
};
int main()
{
    // Actual SDL device lifecycle, with a named dummy backend; no audible sound.
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    Music music;
    register_music_focus_client({&music, Music::suspend, Music::resume});
    platform::linux_runtime::OneSegPcmOutput output;
    auto call = request_exclusive_focus(FocusOwner::Call);
    assert(call && !output.begin(1));
    release_focus(call);
    assert(output.begin(2) && music_blocked());
    assert(!output.clock().valid);
    assert(!request_focus(FocusOwner::Notification));
    assert(!request_exclusive_focus(FocusOwner::FmRadio));
    std::atomic_bool cancel{false};
    std::array<int16_t, 1024> samples;
    samples.fill(10000);
    output.setVolume(0);
    assert(output.write(samples.data(), samples.size(), 0, 2, cancel));
    output.setVolume(100);
    assert(output.write(samples.data(), samples.size(), 21333, 2, cancel));
    auto clock = output.clock();
    assert(clock.generation == 2 && clock.valid && clock.pts_us <= 42666);
    assert(output.write(samples.data(), samples.size(), 2000000, 2, cancel));
    clock = output.clock();
    assert(clock.valid && clock.pts_us > 1900000 && clock.pts_us <= 2021333);
    assert(output.write(samples.data(), samples.size(), INT64_MIN, 2, cancel));
    assert(!output.clock().valid);
    assert(output.write(samples.data(), samples.size(), 3000000, 2, cancel));
    assert(output.clock().valid && output.clock().pts_us > 2900000);
    assert(output.end(false) && !music_blocked());
    assert(!output.clock().valid && !output.clock().generation);
    assert(output.begin(3));
    cancel = true;
    assert(!output.write(samples.data(), samples.size(), 0, 3, cancel));
    assert(output.end(true) && !music_blocked());
    music.ack = false;
    assert(!output.begin(4) && !music_blocked());
    music.ack = true;
    setenv("TRAIL_MATE_AUDIO_OUTPUT_DEVICE", "nonexistent-oneseg-test-device", 1);
    assert(!output.begin(5) && !music_blocked());
    assert(std::strstr(output.lastError(), "Cannot open"));
    unsetenv("TRAIL_MATE_AUDIO_OUTPUT_DEVICE");
    assert(output.begin(6));
    cancel = false;
    assert(!output.write(samples.data(), samples.size(), 0, 5, cancel));
    assert(!output.end(true) && !music_blocked());
    assert(music.suspended == music.resumed);
    register_music_focus_client({});
}
