#include "platform/ui/screen_runtime.h"

#include "platform/ui/settings_store.h"

namespace platform::ui::screen
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr uint32_t kScreenTimeoutDefaultMs = 30000UL;
constexpr uint32_t kScreenTimeoutMinMs = 15000UL;
constexpr uint32_t kScreenTimeoutMaxMs = 300000UL;

bool s_sleep_disabled = false;

uint32_t normalize_timeout_ms(uint32_t timeout_ms)
{
    if (timeout_ms < kScreenTimeoutMinMs)
    {
        return kScreenTimeoutDefaultMs;
    }
    if (timeout_ms > kScreenTimeoutMaxMs)
    {
        return kScreenTimeoutMaxMs;
    }
    return timeout_ms;
}

} // namespace

uint32_t clamp_timeout_ms(uint32_t timeout_ms)
{
    return normalize_timeout_ms(timeout_ms);
}

uint32_t timeout_ms()
{
    return normalize_timeout_ms(
        ::platform::ui::settings_store::get_uint(kSettingsNs, kScreenTimeoutKey, kScreenTimeoutDefaultMs));
}

uint16_t timeout_secs()
{
    return static_cast<uint16_t>(timeout_ms() / 1000U);
}

void set_timeout_ms(uint32_t timeout_ms)
{
    ::platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, normalize_timeout_ms(timeout_ms));
}

void init(const Hooks&)
{
}

bool is_sleeping()
{
    return false;
}

bool is_sleep_disabled()
{
    return s_sleep_disabled;
}

bool is_saver_active()
{
    return false;
}

void wake_saver()
{
}

void enter_from_saver()
{
}

void update_user_activity()
{
}

void disable_sleep()
{
    s_sleep_disabled = true;
}

void enable_sleep()
{
    s_sleep_disabled = false;
}

} // namespace platform::ui::screen
