#pragma once

namespace platform::linux_runtime
{
// Application/UI-thread lifetime binding; detach before owner destruction.
struct SettingsResetBinding
{
    void* owner = nullptr;
    bool (*reset)(void*) = nullptr;
};
void bind_settings_reset(SettingsResetBinding);
void unbind_settings_reset(void* owner);
} // namespace platform::linux_runtime
