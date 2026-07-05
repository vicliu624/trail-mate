#pragma once

#include "boards/t_impulse_plus/t_impulse_plus_board.h"

namespace boards::t_impulse_plus
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

    static bool updateDebounced(bool sampled,
                                DebounceState& state,
                                uint16_t debounce_ms,
                                BoardInputEvent* out_event,
                                uint32_t now_ms);

    mutable DebounceState function_touch_{};
};

} // namespace boards::t_impulse_plus
