#include <cassert>

#include "platform/ui/screen_power_state_machine.h"

namespace
{
using platform::ui::screen_power::Event;
using platform::ui::screen_power::State;
using platform::ui::screen_power::StateMachine;
} // namespace

int main()
{
    StateMachine machine;
    machine.dispatch(Event::Initialize, 100);

    assert(machine.snapshot().state == State::Awake);
    assert(machine.timeout_ms() == StateMachine::kDefaultTimeoutMs);

    auto sleep_effects = machine.dispatch(Event::Tick, 60100);
    assert(sleep_effects.sleep_display);
    assert(machine.snapshot().state == State::Sleeping);

    auto wake_effects = machine.dispatch(Event::WakeInput, 60200);
    assert(wake_effects.wake_display);
    assert(wake_effects.show_saver);
    assert(machine.snapshot().state == State::WakePreview);

    auto non_confirming_input = machine.dispatch(Event::Input, 60210);
    assert(!non_confirming_input.show_main_menu);
    assert(machine.snapshot().state == State::WakePreview);

    auto repeat_input = machine.dispatch(Event::Input, 60300);
    assert(!repeat_input.show_main_menu);
    assert(machine.snapshot().state == State::WakePreview);

    auto confirm_effects = machine.dispatch(Event::ConfirmInput, 60600);
    assert(confirm_effects.hide_saver);
    assert(!confirm_effects.show_main_menu);
    assert(machine.snapshot().state == State::Awake);

    machine.dispatch(Event::Tick, 120700);
    assert(machine.snapshot().state == State::Sleeping);
    machine.dispatch(Event::WakeInput, 120800);
    assert(machine.snapshot().state == State::WakePreview);
    auto timeout_effects = machine.dispatch(Event::Tick, 123800);
    assert(timeout_effects.sleep_display);
    assert(timeout_effects.hide_saver);
    assert(machine.snapshot().state == State::Sleeping);

    auto sleeping_confirm_effects = machine.dispatch(Event::ConfirmInput, 124000);
    assert(sleeping_confirm_effects.wake_display);
    assert(sleeping_confirm_effects.hide_saver);
    assert(!sleeping_confirm_effects.show_main_menu);
    assert(machine.snapshot().state == State::Awake);

    machine.dispatch(Event::DisableSleep, 124100);
    assert(machine.snapshot().state == State::Awake);
    assert(machine.snapshot().sleep_disable_depth == 1);
    assert(!machine.dispatch(Event::Tick, 999999).sleep_display);
    machine.dispatch(Event::EnableSleep, 124200);
    assert(machine.snapshot().sleep_disable_depth == 0);

    machine.set_timeout_ms(1);
    assert(machine.timeout_ms() == StateMachine::kDefaultTimeoutMs);
    machine.set_timeout_ms(400000);
    assert(machine.timeout_ms() == StateMachine::kMaxTimeoutMs);

    // Settings and existing saved configurations encode "Always" as 300000.
    StateMachine always(300000);
    always.dispatch(Event::Initialize, 100);
    assert(!always.dispatch(Event::Tick, 300100).sleep_display);
    assert(!always.dispatch(Event::Tick, 86400100).sleep_display);
    assert(!always.dispatch(Event::Tick, UINT32_MAX).sleep_display);
    assert(!always.dispatch(Event::Tick, 1000).sleep_display);
    assert(always.snapshot().state == State::Awake);
    always.dispatch(Event::DisableSleep, 1100);
    always.dispatch(Event::EnableSleep, 1200);
    assert(!always.dispatch(Event::Tick, 900000).sleep_display);
    // Selecting a timed option again must restore normal idle sleep.
    always.set_timeout_ms(15000);
    always.dispatch(Event::Activity, 900000);
    assert(!always.dispatch(Event::Tick, 914999).sleep_display);
    assert(always.dispatch(Event::Tick, 915000).sleep_display);
    // Applying Always to a sleeping/previewing state must leave it awake.
    always.set_timeout_ms(300000);
    assert(always.dispatch(Event::Tick, 915001).wake_display);
    assert(always.snapshot().state == State::Awake);
    always.set_timeout_ms(15000);
    always.dispatch(Event::Tick, 1000000);
    always.dispatch(Event::WakeInput, 1000001);
    always.set_timeout_ms(300000);
    const auto leave_preview = always.dispatch(Event::Tick, 1004001);
    assert(leave_preview.hide_saver && !leave_preview.sleep_display);
    assert(always.snapshot().state == State::Awake);

    return 0;
}
