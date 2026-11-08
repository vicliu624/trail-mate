#pragma once
#include "lvgl.h"
namespace ui::input::cardputer
{
struct State
{
    uint32_t translated = 0;
    bool consumed = false;
    bool calculator_active = false;
    bool calculator_controls = false;
};
// Runs on the UI thread. Remapping is latched through release even if the
// pressed action changes page/focus. Text editors retain their physical keys.
bool handle_key(uint32_t& key, bool pressed, lv_group_t* group, State& state);
} // namespace ui::input::cardputer
