#include "platform/linux/sdl_pcm_input.h"
#include "platform/linux/sdl_pcm_output.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <thread>

namespace
{
bool number(std::string_view text, unsigned& value)
{
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
} // namespace
int main(int argc, char** argv)
{
    unsigned milliseconds = 0, rate = 15000;
    const char* input_device = nullptr;
    bool playback = false, list_devices = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view option = argv[i];
        if (option == "--play-silence") playback = true;
        else if (option == "--list-devices") list_devices = true;
        else if (option == "--input" && i + 1 < argc) input_device = argv[++i];
        else if (option == "--capture-ms" && i + 1 < argc && number(argv[i + 1], milliseconds)) ++i;
        else if (option == "--rate" && i + 1 < argc && number(argv[i + 1], rate)) ++i;
        else
        {
            std::fprintf(stderr, "Invalid audio probe argument\n");
            return 2;
        }
    }
    if ((milliseconds == 0 ? !list_devices : milliseconds < 100 || milliseconds > 5000) || rate < 8000 || rate > 48000)
    {
        std::fprintf(stderr, "Usage: trailmate-linux-audio-probe [--list-devices] [--capture-ms 100..5000] [--rate 8000..48000] [--input NAME] [--play-silence]\n");
        return 2;
    }
    using namespace platform::linux_runtime;
    using Clock = std::chrono::steady_clock;
    SdlPcmInput input;
    const auto driver = SDL_GetCurrentAudioDriver();
    std::printf("audio_driver=%s capture_available=%d rate=%u\n", driver ? driver : "none", input.available(), rate);
    if (list_devices)
    {
        for (int capture = 0; capture <= 1; ++capture)
            for (int index = 0; index < SDL_GetNumAudioDevices(capture); ++index)
            {
                const auto* name = SDL_GetAudioDeviceName(index, capture);
                std::printf("%s_device[%d]=%s\n", capture ? "capture" : "playback", index, name ? name : "<unavailable>");
            }
        if (!milliseconds) return driver ? 0 : 1;
    }
    if (!input.begin(rate, input_device))
    {
        std::fprintf(stderr, "Capture failed: %s\n", input.last_error());
        return 1;
    }
    std::array<int16_t, 1024> samples{};
    uint64_t total = 0, nonzero = 0;
    unsigned peak = 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
    while (Clock::now() < deadline && input.active())
    {
        const auto count = input.read(samples.data(), samples.size());
        total += count;
        for (size_t i = 0; i < count; ++i)
        {
            nonzero += samples[i] != 0;
            peak = std::max(peak, static_cast<unsigned>(std::abs(static_cast<int>(samples[i]))));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto dropped = input.dropped_samples();
    const bool active = input.active();
    input.end();
    const bool stopped = !input.active() && input.buffered_samples() == 0;
    std::printf("capture_samples=%llu nonzero_samples=%llu peak=%u dropped=%llu active=%d stopped=%d\n",
                static_cast<unsigned long long>(total), static_cast<unsigned long long>(nonzero), peak,
                static_cast<unsigned long long>(dropped), active, stopped);
    bool output_ok = true;
    if (playback)
    {
        // Queue only digital silence: validates playback transport, not acoustics.
        SdlPcmOutput output(false);
        output_ok = output.begin(rate);
        samples.fill(0);
        for (size_t left = rate / 10; output_ok && left;)
        {
            const auto count = std::min(left, samples.size());
            output_ok = output.write(samples.data(), count);
            left -= count;
        }
        const auto drain_deadline = Clock::now() + std::chrono::seconds(2);
        bool drained = false;
        while (output_ok && Clock::now() < drain_deadline && !(drained = output.drain()))
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        output_ok = output_ok && drained && !output.failed();
        output.end();
        std::printf("playback_silence_drained=%d stopped=%d\n", output_ok, !output.active());
    }
    // Samples are never retained, logged, written to a file or transmitted.
    return total >= uint64_t(rate) * milliseconds / 4000 && active && stopped && !dropped && output_ok ? 0 : 1;
}
