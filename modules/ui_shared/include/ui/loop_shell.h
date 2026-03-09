#pragma once

#include <stdint.h>

namespace ui::loop_shell
{

struct Hooks
{
    uint32_t (*now_ms)() = nullptr;
    bool (*is_overlay_active)() = nullptr;
    void (*handle_power_button)() = nullptr;
    void (*update_runtime)() = nullptr;
    void (*display_tick_if_due)(uint32_t now_ms) = nullptr;
    void (*yield_now)() = nullptr;
    void (*sleep_ms)(uint32_t ms) = nullptr;
    uint32_t overlay_sleep_ms = 10;
    uint32_t idle_sleep_ms = 2;
};

void tick(const Hooks& hooks);

} // namespace ui::loop_shell
