#pragma once

#include "lvgl.h"
#include <cstddef>

namespace ui::menu::dashboard
{
// Stored in the visible widget's PSRAM state. The launcher retains its input
// group so the wheel can page widgets; keyboard selection never steals it.
struct DashboardKeyFocus
{
    lv_obj_t* items[5]{};
    unsigned count = 0;
    unsigned selected = 0;
    bool editing = false;

    void add(lv_obj_t* item)
    {
        if (count == 5) return;
        items[count++] = item;
#if defined(ARDUINO_T_LORA_PAGER) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        lv_obj_set_style_outline_width(item, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_pad(item, 1, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_color(item, lv_color_hex(0x6B4A1E), LV_STATE_FOCUS_KEY);
#endif
    }
    lv_obj_t* current() const { return count ? items[selected] : nullptr; }
    bool available(unsigned index) const
    {
        return index < count && items[index] && !lv_obj_has_state(items[index], LV_STATE_DISABLED);
    }
    void show()
    {
#if defined(ARDUINO_T_LORA_PAGER) || defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        for (unsigned i = 0; i < count; ++i)
        {
            constexpr auto state = static_cast<lv_state_t>(LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
            if (i == selected && available(i)) lv_obj_add_state(items[i], state);
            else lv_obj_remove_state(items[i], state);
        }
#endif
    }
    void move(int direction)
    {
        editing = false;
        for (unsigned i = 0; i < count; ++i)
        {
            selected = (selected + count + direction) % count;
            if (available(selected)) break;
        }
        show();
    }
    void ensure()
    {
        if (count && !available(selected)) move(1);
        else show();
    }
};
} // namespace ui::menu::dashboard
