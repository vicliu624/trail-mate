#include "ui/loop_shell.h"

namespace ui::loop_shell
{

void tick(const Hooks& hooks)
{
    const uint32_t now_ms = hooks.now_ms ? hooks.now_ms() : 0;

    if (hooks.is_overlay_active && hooks.is_overlay_active())
    {
        if (hooks.display_tick_if_due)
        {
            hooks.display_tick_if_due(now_ms);
        }
        if (hooks.yield_now)
        {
            hooks.yield_now();
        }
        if (hooks.sleep_ms)
        {
            hooks.sleep_ms(hooks.overlay_sleep_ms);
        }
        return;
    }

    if (hooks.handle_power_button)
    {
        hooks.handle_power_button();
    }

    if (hooks.update_runtime)
    {
        hooks.update_runtime();
    }

    if (hooks.display_tick_if_due)
    {
        hooks.display_tick_if_due(now_ms);
    }

    if (hooks.sleep_ms)
    {
        hooks.sleep_ms(hooks.idle_sleep_ms);
    }
}

} // namespace ui::loop_shell
