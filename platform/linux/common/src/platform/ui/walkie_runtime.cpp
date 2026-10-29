#include "platform/ui/walkie_runtime.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/walkie_service.h"
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
#include "platform/audio/media_volume.h"
#endif

namespace platform::ui::walkie
{
bool is_supported() { return ::platform::linux_runtime::native_walkie_built(); }
CapabilityStatus capability_status()
{
    using namespace ::platform::linux_runtime;
    if (!native_walkie_built()) return {CapabilityState::Unsupported, "Native walkie audio is not built."};
    if (*native_walkie_error()) return {CapabilityState::Error, native_walkie_error()};
    if (!native_audio_snapshot().playback_available)
        return {CapabilityState::Degraded, "FSK/Codec2 backend built; audio readiness is not established."};
    const auto state = native_walkie_status();
    if (!state.active || state.freq_mhz <= 0) return {CapabilityState::Degraded, "FSK/Codec2 backend built; no active radio session."};
    return {CapabilityState::Available, "Native WT v2 FSK/Codec2 radio session is active."};
}
bool start() { return ::platform::linux_runtime::start_native_walkie(); }
void stop() { ::platform::linux_runtime::stop_native_walkie(); }
bool is_active() { return ::platform::linux_runtime::native_walkie_status().active; }
void set_ptt(bool pressed) { ::platform::linux_runtime::native_walkie_ptt(pressed); }
bool set_monitor_enabled(bool enabled) { return ::platform::linux_runtime::native_walkie_monitor(enabled); }
bool monitor_enabled() { return ::platform::linux_runtime::native_walkie_status().monitor_enabled; }
int volume()
{
#if defined(TRAIL_MATE_LINUX_HAS_MEDIA)
    return ::platform::audio::media_volume(45);
#else
    return 0;
#endif
}
Status get_status() { return ::platform::linux_runtime::native_walkie_status(); }
const char* last_error() { return ::platform::linux_runtime::native_walkie_error(); }
} // namespace platform::ui::walkie
