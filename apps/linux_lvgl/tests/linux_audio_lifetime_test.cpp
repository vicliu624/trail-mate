#include "platform/linux/sdl_pcm_input.h"
#include "platform/linux/sdl_pcm_output.h"
#include <SDL.h>
#include <array>
#include <atomic>
#include <barrier>
#include <cassert>
#include <chrono>
#include <memory>
#include <thread>

#if defined(TRAIL_MATE_TEST_AUDIO_LIFETIME_GATE)
namespace
{
std::atomic<unsigned> active_calls{0}, maximum_calls{0};
void lifecycleCall()
{
    const auto active = ++active_calls;
    auto maximum = maximum_calls.load();
    while (maximum < active && !maximum_calls.compare_exchange_weak(maximum, active))
    {
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    --active_calls;
}
} // namespace
// Interpose only the lifecycle/enumeration seam used by the real PCM owners.
// This makes concurrent entry observable even with a permissive dummy backend.
extern "C" int SDLCALL SDL_InitSubSystem(Uint32 flags)
{
    if (flags & SDL_INIT_AUDIO) lifecycleCall();
    return 0;
}
extern "C" void SDLCALL SDL_QuitSubSystem(Uint32 flags)
{
    if (flags & SDL_INIT_AUDIO) lifecycleCall();
}
extern "C" int SDLCALL SDL_GetNumAudioDevices(int) { return 1; }
extern "C" Uint32 SDLCALL SDL_WasInit(Uint32) { return 0; }
#endif

int main()
{
    using namespace platform::linux_runtime;
    assert(SDL_InitSubSystem(SDL_INIT_EVENTS) == 0);
    std::barrier phase(3);
    std::atomic<unsigned> enumerations{0};
    std::array<std::thread, 3> workers;
    for (auto& worker : workers)
        worker = std::thread([&]
                             {
            for (unsigned round = 0; round < 100; ++round)
            {
                phase.arrive_and_wait();
                auto input = std::make_unique<SdlPcmInput>();
                auto output = std::make_unique<SdlPcmOutput>(false);
                phase.arrive_and_wait();
                if (input->available() && output->supported()) ++enumerations;
                phase.arrive_and_wait();
                input.reset();
                output.reset();
                phase.arrive_and_wait();
            } });
    for (auto& worker : workers) worker.join();
    assert(enumerations == 300);
    assert(SDL_WasInit(SDL_INIT_AUDIO) == 0);
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
#if defined(TRAIL_MATE_TEST_AUDIO_LIFETIME_GATE)
    assert(maximum_calls == 1);
#endif
}
