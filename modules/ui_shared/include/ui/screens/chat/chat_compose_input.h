#pragma once
#include "chat_compose_layout.h"
#include "lvgl.h"

namespace chat::ui::compose::input
{

struct State
{
    bool encoder_enter_focus_send = true;
};

void bind_textarea_events(const layout::Widgets& w, void* user_data,
                          lv_event_cb_t key_cb, lv_event_cb_t text_cb);

void setup_default_group_focus(const layout::Widgets& w);

} // namespace chat::ui::compose::input
