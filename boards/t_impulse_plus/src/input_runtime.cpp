#include "boards/t_impulse_plus/input_runtime.h"

#include "boards/t_impulse_plus/board_profile.h"

#include <Arduino.h>

namespace boards::t_impulse_plus
{
namespace
{

bool readTouchPin()
{
    const auto& input = kBoardProfile.input;
    if (input.function_touch < 0)
    {
        return false;
    }
    pinMode(input.function_touch, input.use_pullup ? INPUT_PULLUP : INPUT);
    const bool high = digitalRead(input.function_touch) == HIGH;
    return input.active_high ? high : !high;
}

} // namespace

bool InputRuntime::pollSnapshot(BoardInputSnapshot* out_snapshot) const
{
    if (!out_snapshot)
    {
        return false;
    }

    BoardInputSnapshot snapshot{};
    snapshot.function_touch = readTouchPin();
    snapshot.any_activity = snapshot.function_touch;
    *out_snapshot = snapshot;
    return snapshot.any_activity;
}

uint16_t InputRuntime::debounceMs() const
{
    return kBoardProfile.input.debounce_ms;
}

bool InputRuntime::updateDebounced(bool sampled,
                                   DebounceState& state,
                                   uint16_t debounce_ms,
                                   BoardInputEvent* out_event,
                                   uint32_t now_ms)
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
        out_event->key = BoardInputKey::Function;
        out_event->pressed = state.stable;
        out_event->timestamp_ms = now_ms;
    }
    return true;
}

bool InputRuntime::pollEvent(BoardInputEvent* out_event)
{
    if (out_event)
    {
        *out_event = BoardInputEvent{};
    }

    const uint32_t now_ms = millis();
    return updateDebounced(readTouchPin(), function_touch_, debounceMs(), out_event, now_ms);
}

} // namespace boards::t_impulse_plus
