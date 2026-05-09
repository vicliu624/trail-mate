#include "platform/ui/screen_runtime.h"

#include "platform/ui/settings_store.h"

#include <chrono>
#include <cstdint>

namespace platform::ui::screen
{
namespace
{

using Clock = std::chrono::steady_clock;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr uint32_t kScreenTimeoutMinMs = 10000U;
constexpr uint32_t kScreenTimeoutMaxMs = 300000U;
constexpr uint32_t kScreenTimeoutDefaultMs = 60000U;

Hooks s_hooks{};
Clock::time_point s_last_activity = Clock::now();
uint32_t s_sleep_disable_depth = 0;
bool s_sleeping = false;

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

uint32_t current_timeout_ms()
{
    return normalize_timeout_ms(
        ::platform::ui::settings_store::get_uint(kSettingsNs, kScreenTimeoutKey, kScreenTimeoutDefaultMs));
}

bool should_consider_sleep_state()
{
    return s_sleep_disable_depth == 0 && current_timeout_ms() >= kScreenTimeoutMinMs;
}

bool refresh_idle_state()
{
    if (!should_consider_sleep_state())
    {
        s_sleeping = false;
        return false;
    }

    const auto idle_for = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s_last_activity);
    s_sleeping = idle_for.count() >= current_timeout_ms();
    return s_sleeping;
}

} // namespace

uint32_t clamp_timeout_ms(uint32_t timeout_ms)
{
    return normalize_timeout_ms(timeout_ms);
}

uint32_t timeout_ms()
{
    return current_timeout_ms();
}

uint16_t timeout_secs()
{
    return static_cast<uint16_t>(timeout_ms() / 1000U);
}

void set_timeout_ms(uint32_t timeout_ms)
{
    ::platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, normalize_timeout_ms(timeout_ms));
    refresh_idle_state();
}

void init(const Hooks& hooks)
{
    s_hooks = hooks;
    s_last_activity = Clock::now();
    s_sleeping = false;
}

bool is_sleeping()
{
    return refresh_idle_state();
}

bool is_sleep_disabled()
{
    return s_sleep_disable_depth > 0;
}

bool is_saver_active()
{
    return is_sleeping();
}

void wake_saver()
{
    const bool was_sleeping = s_sleeping;
    s_last_activity = Clock::now();
    s_sleeping = false;
    if (was_sleeping && s_hooks.on_wake_from_sleep)
    {
        s_hooks.on_wake_from_sleep();
    }
}

void enter_from_saver()
{
    wake_saver();
}

void update_user_activity()
{
    wake_saver();
}

void disable_sleep()
{
    if (s_sleep_disable_depth < UINT32_MAX)
    {
        ++s_sleep_disable_depth;
    }
    s_sleeping = false;
}

void enable_sleep()
{
    if (s_sleep_disable_depth > 0)
    {
        --s_sleep_disable_depth;
    }
    refresh_idle_state();
}

} // namespace platform::ui::screen
