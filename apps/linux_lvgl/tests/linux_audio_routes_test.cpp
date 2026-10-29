#include "platform/linux/sdl_pcm_input.h"
#include "platform/linux/sdl_pcm_output.h"
#include <SDL.h>
#include <cassert>
#include <cstdlib>
#include <string>

int main()
{
    using namespace platform::linux_runtime;
    SdlPcmInput input;
    SdlPcmOutput output(false);
    assert(input.available() && output.supported());
    const std::string capture_name = SDL_GetAudioDeviceName(0, 1);
    const std::string playback_name = SDL_GetAudioDeviceName(0, 0);
    assert(setenv("TRAIL_MATE_AUDIO_INPUT_DEVICE", "TrailMate-missing-input", 1) == 0);
    assert(!input.begin(8000) && !input.active());
    // A workflow's explicit selection overrides the common capture route.
    assert(input.begin(8000, capture_name.c_str()) && input.active());
    input.end();
    assert(setenv("TRAIL_MATE_AUDIO_INPUT_DEVICE", capture_name.c_str(), 1) == 0);
    assert(input.begin(8000) && input.active());
    input.end();
    assert(setenv("TRAIL_MATE_AUDIO_OUTPUT_DEVICE", "TrailMate-missing-output", 1) == 0);
    assert(!output.begin(8000) && !output.active());
    assert(setenv("TRAIL_MATE_AUDIO_OUTPUT_DEVICE", playback_name.c_str(), 1) == 0);
    assert(output.begin(8000) && output.active() && !output.failed());
    output.end();
    unsetenv("TRAIL_MATE_AUDIO_INPUT_DEVICE");
    unsetenv("TRAIL_MATE_AUDIO_OUTPUT_DEVICE");
    assert(input.begin(8000) && output.begin(8000));
    input.end();
    output.end();
}
