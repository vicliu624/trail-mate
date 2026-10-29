#pragma once
#include <SDL.h>
#include <mutex>

namespace platform::linux_runtime::detail
{
// SDL's subsystem reference counts and backend initialization are process-wide.
// Every PCM owner must use the same gate, including transient capability probes.
inline std::mutex audio_lifecycle_mutex;
inline bool acquireAudioSubsystem()
{
    std::lock_guard<std::mutex> lock(audio_lifecycle_mutex);
    return SDL_InitSubSystem(SDL_INIT_AUDIO) == 0;
}
inline void releaseAudioSubsystem()
{
    std::lock_guard<std::mutex> lock(audio_lifecycle_mutex);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
} // namespace platform::linux_runtime::detail
