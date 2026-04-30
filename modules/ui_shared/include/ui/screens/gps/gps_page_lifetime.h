#pragma once

#include "lvgl.h"

namespace gps::ui::lifetime
{

void mark_alive(lv_obj_t* root, lv_group_t* app_group);
bool is_alive();

void bind_root_delete_hook();

lv_timer_t* add_timer(lv_timer_cb_t cb, uint32_t period_ms, void* user_data);
void remove_timer(lv_timer_t* timer);
void clear_timers();

} // namespace gps::ui::lifetime
