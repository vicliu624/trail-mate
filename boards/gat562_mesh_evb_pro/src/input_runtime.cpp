#include "boards/gat562_mesh_evb_pro/input_runtime.h"

#include "boards/gat562_mesh_evb_pro/board_profile.h"

#include <Arduino.h>

namespace boards::gat562_mesh_evb_pro
{
namespace
{

bool readActiveLowPin(int pin, bool use_pullup)
{
    if (pin < 0)
    {
        return false;
    }
    pinMode(pin, use_pullup ? INPUT_PULLUP : INPUT);
    return digitalRead(pin) == LOW;
}

} // namespace

bool InputRuntime::pollSnapshot(BoardInputSnapshot* out_snapshot) const
{
    if (!out_snapshot)
    {
        return false;
    }

    const auto& inputs = kBoardProfile.inputs;
    BoardInputSnapshot snapshot{};
    snapshot.button_primary = readActiveLowPin(inputs.button_primary, inputs.buttons_need_pullup);
    snapshot.button_secondary = readActiveLowPin(inputs.button_secondary, inputs.buttons_need_pullup);
    snapshot.joystick_up = readActiveLowPin(inputs.joystick_up, inputs.joystick_need_pullup);
    snapshot.joystick_down = readActiveLowPin(inputs.joystick_down, inputs.joystick_need_pullup);
    snapshot.joystick_left = readActiveLowPin(inputs.joystick_left, inputs.joystick_need_pullup);
    snapshot.joystick_right = readActiveLowPin(inputs.joystick_right, inputs.joystick_need_pullup);
    snapshot.joystick_press = readActiveLowPin(inputs.joystick_press, inputs.joystick_need_pullup);
    snapshot.any_activity = snapshot.button_primary || snapshot.button_secondary ||
                            snapshot.joystick_up || snapshot.joystick_down ||
                            snapshot.joystick_left || snapshot.joystick_right ||
                            snapshot.joystick_press;

    *out_snapshot = snapshot;
    return snapshot.any_activity;
}

uint16_t InputRuntime::debounceMs() const
{
    return kBoardProfile.inputs.debounce_ms;
}

bool InputRuntime::updateDebounced(bool sampled,
                                   DebounceState& state,
                                   uint16_t debounce_ms,
                                   BoardInputKey key,
                                   BoardInputEvent* out_event,
                                   uint32_t now_ms,
                                   State& runtime_state)
{
    if (sampled != state.sampled)
    {
        state.sampled = sampled;
        state.changed_at_ms = now_ms;
    }

    if (state.stable == state.sampled)
    {
        return false;
    }

    if ((now_ms - state.changed_at_ms) < debounce_ms)
    {
        return false;
    }

    state.stable = state.sampled;
    if (out_event)
    {
        out_event->key = key;
        out_event->pressed = state.stable;
        out_event->timestamp_ms = now_ms;
    }
    if (state.stable)
    {
        runtime_state.last_activity_ms = now_ms;
    }
    return true;
}

bool InputRuntime::pollEvent(BoardInputEvent* out_event)
{
    if (out_event)
    {
        *out_event = BoardInputEvent{};
    }

    BoardInputSnapshot current{};
    (void)pollSnapshot(&current);
    state_.snapshot = current;

    const uint32_t now_ms = millis();
    const uint16_t debounce_ms = debounceMs();

    return updateDebounced(current.button_primary, state_.button_primary, debounce_ms,
                           BoardInputKey::PrimaryButton, out_event, now_ms, state_) ||
           updateDebounced(current.button_secondary, state_.button_secondary, debounce_ms,
                           BoardInputKey::SecondaryButton, out_event, now_ms, state_) ||
           updateDebounced(current.joystick_up, state_.joystick_up, debounce_ms,
                           BoardInputKey::JoystickUp, out_event, now_ms, state_) ||
           updateDebounced(current.joystick_down, state_.joystick_down, debounce_ms,
                           BoardInputKey::JoystickDown, out_event, now_ms, state_) ||
           updateDebounced(current.joystick_left, state_.joystick_left, debounce_ms,
                           BoardInputKey::JoystickLeft, out_event, now_ms, state_) ||
           updateDebounced(current.joystick_right, state_.joystick_right, debounce_ms,
                           BoardInputKey::JoystickRight, out_event, now_ms, state_) ||
           updateDebounced(current.joystick_press, state_.joystick_press, debounce_ms,
                           BoardInputKey::JoystickPress, out_event, now_ms, state_);
}

} // namespace boards::gat562_mesh_evb_pro
