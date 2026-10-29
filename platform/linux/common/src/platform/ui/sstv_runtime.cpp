#include "platform/ui/sstv_runtime.h"
#include "platform/linux/native_audio_service.h"
#include "platform/linux/sstv_service.h"
#include <cstdlib>

namespace platform::ui::sstv
{
bool is_supported() { return ::platform::linux_runtime::native_sstv_built(); }
CapabilityStatus capability_status()
{
    using namespace ::platform::linux_runtime;
    if (!native_sstv_built()) return {CapabilityState::Unsupported, "Native SSTV audio is not built."};
    if (native_sstv_status().state == State::Error) return {CapabilityState::Error, native_sstv_error()};
    if (const auto* file = std::getenv("TRAIL_MATE_SSTV_PCM_FILE"))
        if (*file) return {CapabilityState::Available, "SSTV receives explicitly configured 15 kHz mono PCM."};
    if (!native_audio_snapshot().capture_available)
        return {CapabilityState::Degraded, "SSTV decoder built; microphone readiness is not established."};
    return {CapabilityState::Available, "SSTV receives native microphone PCM through the shared decoder."};
}
bool start() { return ::platform::linux_runtime::start_native_sstv(); }
void stop() { ::platform::linux_runtime::stop_native_sstv(); }
bool is_active() { return ::platform::linux_runtime::native_sstv_active(); }
Status get_status() { return ::platform::linux_runtime::native_sstv_status(); }
const char* last_error() { return ::platform::linux_runtime::native_sstv_error(); }
const char* last_saved_path() { return ::platform::linux_runtime::native_sstv_saved_path(); }
const char* mode_name() { return ::platform::linux_runtime::native_sstv_mode(); }
const uint16_t* framebuffer() { return ::platform::linux_runtime::native_sstv_frame(); }
uint16_t frame_width() { return 288; }
uint16_t frame_height() { return 192; }
} // namespace platform::ui::sstv
