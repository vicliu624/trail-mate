#pragma once

#include "lvgl.h"

namespace ui::components::dialog_frame
{
struct Config
{
    const char* title;
    lv_coord_t width = 304;
    lv_coord_t height = 180;
    lv_event_cb_t on_close = nullptr;
    lv_event_cb_t on_key = nullptr;
    void* context = nullptr;
};

struct Frame
{
    lv_obj_t* overlay;
    lv_obj_t* panel;
    lv_obj_t* body;
    lv_obj_t* close;
};

// The page owns the returned widgets and focus group. No retained controller or
// business data: the fixed title bar and scrolling body are just shared chrome.
Frame create(lv_obj_t* parent, const Config& config);
// Call after populating the body for a compact, content-sized action dialog.
void fit_content_height(const Frame& frame);
// Register X/x on this dialog's controls after the page builds its focus group.
void bind_close_shortcuts(const Frame& frame, lv_group_t* group);
// Handle arrows on modal buttons/checkboxes without taking editor or scroller keys.
inline bool handle_focus_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY) return false;
    const auto key = lv_event_get_key(event);
    auto* target = lv_event_get_target_obj(event);
    if (!(lv_obj_check_type(target, &lv_button_class) || lv_obj_check_type(target, &lv_checkbox_class)) ||
        !(key == LV_KEY_UP || key == LV_KEY_DOWN || key == LV_KEY_LEFT || key == LV_KEY_RIGHT)) return false;
    auto* group = lv_obj_get_group(target);
    if (!group) return false;
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    if (key == LV_KEY_UP || key == LV_KEY_LEFT) lv_group_focus_prev(group);
    else lv_group_focus_next(group);
    if (auto* focused = lv_group_get_focused(group)) lv_obj_scroll_to_view(focused, LV_ANIM_OFF);
    return true;
}
} // namespace ui::components::dialog_frame
