#include "platform/ui/screen_runtime.h"

#include "platform/linux/screen_service.h"
#include "platform/ui/screen_power_state_machine.h"
#include "platform/ui/settings_store.h"

#include <chrono>
#include <cstdint>
#include <mutex>

namespace platform::ui::screen
{
namespace
{

using Clock = std::chrono::steady_clock;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
platform::ui::screen_power::StateMachine s_machine{};
std::mutex s_mutex;
Hooks s_hooks{};
::platform::linux_runtime::ScreenRuntimeOptions s_options;
bool s_configured = false;
bool s_initialized = false;
bool s_stopped = false;
bool s_wake_pending = false;
platform::ui::screen_power::State s_presented = platform::ui::screen_power::State::Awake;

uint32_t now_ms()
{
    if (s_options.now_ms) return s_options.now_ms();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch())
            .count());
}

} // namespace

uint32_t clamp_timeout_ms(uint32_t timeout_ms)
{
    return platform::ui::screen_power::StateMachine::clamp_timeout_ms(timeout_ms);
}

uint32_t timeout_ms()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_machine.timeout_ms();
}

uint16_t timeout_secs()
{
    return static_cast<uint16_t>(timeout_ms() / 1000U);
}

bool supports_app_timeout_setting()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (!s_options.power || s_stopped) return false;
    const auto power = s_options.power->snapshot();
    return power.available && !power.failed;
}

void set_timeout_ms(uint32_t timeout_ms)
{
    const uint32_t normalized =
        platform::ui::screen_power::StateMachine::clamp_timeout_ms(timeout_ms);
    ::platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, normalized);
    std::lock_guard<std::mutex> lock(s_mutex);
    s_machine.set_timeout_ms(normalized);
}

void init(const Hooks& hooks)
{
    const auto timeout = ::platform::ui::settings_store::get_uint(
        kSettingsNs,
        kScreenTimeoutKey,
        platform::ui::screen_power::StateMachine::kDefaultTimeoutMs);
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_initialized || s_stopped) return;
    s_hooks = hooks;
    s_machine.set_timeout_ms(timeout);
    s_machine.dispatch(platform::ui::screen_power::Event::Initialize, now_ms());
    s_initialized = true;
}

bool is_sleeping()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_machine.snapshot().state ==
           platform::ui::screen_power::State::Sleeping;
}

bool is_sleep_disabled()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_machine.snapshot().sleep_disable_depth > 0;
}

bool is_saver_active()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_machine.snapshot().state ==
           platform::ui::screen_power::State::WakePreview;
}

void handle_input()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_machine.dispatch(platform::ui::screen_power::Event::Input, now_ms());
}

void handle_confirm_input()
{
    request_resume();
}

void handle_input_release()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_machine.dispatch(platform::ui::screen_power::Event::InputRelease, now_ms());
}

void wake_for_modal()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_machine.dispatch(platform::ui::screen_power::Event::ModalWake, now_ms());
}

void record_activity()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_machine.dispatch(platform::ui::screen_power::Event::Activity, now_ms());
}

void disable_sleep()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_machine.dispatch(platform::ui::screen_power::Event::DisableSleep, now_ms());
}

void enable_sleep()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_machine.dispatch(platform::ui::screen_power::Event::EnableSleep, now_ms());
}

void request_resume()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_machine.dispatch(platform::ui::screen_power::Event::ConfirmInput, now_ms());
}

ResumeMethod resume_method()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_options.resume_method;
}

} // namespace platform::ui::screen

namespace platform::linux_runtime
{
bool configure_screen_runtime(ScreenRuntimeOptions options)
{
    using namespace ::platform::ui::screen;
    std::lock_guard<std::mutex> lock(s_mutex);
    if (s_configured || s_initialized || s_stopped) return false;
    s_options = std::move(options);
    s_configured = true;
    return true;
}

void service_screen_runtime()
{
    using namespace ::platform::ui::screen;
    using namespace ::platform::ui::screen_power;
    Hooks hooks;
    std::shared_ptr<OutputPowerRuntime> power;
    State before;
    State next;
    bool wake = false;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_initialized || s_stopped) return;
        hooks = s_hooks;
        power = s_options.power;
        const auto actual = power ? power->snapshot() : OutputPowerSnapshot{};
        // Loss of the selected output or a failed operation must not strand the
        // UI in a software-only sleep state. A healthy port enables timeouts.
        s_machine.dispatch(actual.available && !actual.failed ? Event::Tick : Event::ModalWake, now_ms());
        before = s_presented;
        next = s_machine.snapshot().state;
        const bool leaving_sleep = before == State::Sleeping && next != State::Sleeping;
        if (leaving_sleep) s_wake_pending = true;
        // An earlier off request may still be in flight while readback says on.
        // Always enqueue the first wake, even in that case.
        if (!leaving_sleep && s_wake_pending && actual.available && actual.powered) s_wake_pending = false;
        wake = s_wake_pending;
        s_presented = next;
    }
    if (next == State::Sleeping && before != State::Sleeping && (!power || !power->request(false)))
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_machine.dispatch(Event::ModalWake, now_ms());
        s_presented = next = State::Awake;
        s_wake_pending = true;
        wake = true;
    }
    if (wake && power) (void)power->request(true);
    // Hooks may reenter the public screen ports. Invoke them without the state
    // mutex, only on the UI thread; background callers only mutate the machine.
    if (before != next)
    {
        if (next == State::WakePreview)
        {
            if (hooks.show_screen_saver) hooks.show_screen_saver();
            else if (hooks.on_wake_from_sleep) hooks.on_wake_from_sleep();
            if (hooks.present_screen_saver) hooks.present_screen_saver();
        }
        else if (hooks.hide_screen_saver) hooks.hide_screen_saver();
    }
}

void shutdown_screen_runtime()
{
    using namespace ::platform::ui::screen;
    std::shared_ptr<OutputPowerRuntime> power;
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_stopped = true;
        s_initialized = false;
        s_hooks = {};
        s_machine.dispatch(::platform::ui::screen_power::Event::Initialize, now_ms());
        power = std::move(s_options.power);
    }
    // Do not join the hardware worker while holding the state mutex.
    power.reset();
}
} // namespace platform::linux_runtime
