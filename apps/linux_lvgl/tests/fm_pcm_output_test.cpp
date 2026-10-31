#include "platform/audio/audio_focus.h"
#include "platform/linux/fm_pcm_output.h"
#include <cassert>

using namespace platform::linux_runtime;
int main()
{
    FmPcmOutput fm;
    std::atomic_bool cancel{false};
    int16_t samples[120]{};
    assert(!fm.begin(0));
    auto other = platform::audio::request_exclusive_focus(platform::audio::FocusOwner::Walkie);
    assert(other && !fm.begin(1));
    platform::audio::release_focus(other);
    assert(fm.begin(1));
    assert(platform::audio::music_blocked());
    assert(!platform::audio::request_exclusive_focus(platform::audio::FocusOwner::Television));
    assert(!fm.begin(2));
    assert(fm.write(samples, 120, 1, cancel));
    assert(!fm.write(samples, 120, 2, cancel));
    fm.setVolume(0);
    assert(fm.write(samples, 120, 1, cancel));
    fm.setVolume(200);
    cancel = true;
    assert(!fm.write(samples, 120, 1, cancel));
    fm.end();
    fm.end();
    assert(!platform::audio::music_blocked());
    cancel = false;
    assert(fm.begin(2));
    assert(!fm.write(samples, 120, 1, cancel));
    assert(fm.write(samples, 120, 2, cancel));
    fm.end();
    assert(!platform::audio::music_blocked());
}
