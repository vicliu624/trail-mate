#include "platform/ui/settings_reset_runtime.h"
#include "platform/linux/settings_reset_binding.h"

namespace
{
platform::linux_runtime::SettingsResetBinding binding;
}
namespace platform::linux_runtime
{
void bind_settings_reset(SettingsResetBinding hooks) { binding = hooks; }
void unbind_settings_reset(void* owner)
{
    if (binding.owner == owner) binding = {};
}
} // namespace platform::linux_runtime
namespace platform::ui::settings_reset
{
bool begin() { return binding.owner && binding.reset && binding.reset(binding.owner); }
void finish()
{
    // Linux restarts with exec after orderly teardown. Keep the committed
    // reset quiescent through that teardown so old owners cannot restore it.
}
} // namespace platform::ui::settings_reset
