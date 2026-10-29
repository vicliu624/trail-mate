#pragma once

#include "platform/linux/output_power.h"
#include "platform/ui/screen_runtime.h"
#include <cstdint>
#include <functional>
#include <memory>

namespace platform::linux_runtime
{
struct ScreenRuntimeOptions
{
    std::shared_ptr<OutputPowerRuntime> power;
    platform::ui::screen::ResumeMethod resume_method = platform::ui::screen::ResumeMethod::SpaceKey;
    // Optional monotonic clock port. The callback must not reenter this service.
    std::function<uint32_t()> now_ms;
};
// Configure before UI initialization and before other threads use screen ports.
// A null power port leaves automatic hardware sleep unavailable.
bool configure_screen_runtime(ScreenRuntimeOptions options);
// UI thread only: dispatch timeouts, reconcile hardware and invoke saver hooks.
void service_screen_runtime();
// UI thread only, before deleting UI objects. Releases restoration ownership.
void shutdown_screen_runtime();
} // namespace platform::linux_runtime
