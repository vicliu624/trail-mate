#pragma once

#include "boards/gat562_mesh_evb_pro/gat562_board.h"

namespace boards::gat562_mesh_evb_pro
{

class InputRuntime
{
  public:
    bool pollSnapshot(BoardInputSnapshot* out_snapshot) const;
    uint16_t debounceMs() const;
    bool pollEvent(BoardInputEvent* out_event);

  private:
    struct DebounceState
    {
        bool stable = false;
        bool sampled = false;
        uint32_t changed_at_ms = 0;
    };

    struct State
    {
        uint32_t last_activity_ms = 0;
        BoardInputSnapshot snapshot{};
        DebounceState button_primary{};
        DebounceState button_secondary{};
        DebounceState joystick_up{};
        DebounceState joystick_down{};
        DebounceState joystick_left{};
        DebounceState joystick_right{};
        DebounceState joystick_press{};
    };

    static bool updateDebounced(bool sampled,
                                DebounceState& state,
                                uint16_t debounce_ms,
                                BoardInputKey key,
                                BoardInputEvent* out_event,
                                uint32_t now_ms,
                                State& runtime_state);

    mutable State state_{};
};

} // namespace boards::gat562_mesh_evb_pro
