#pragma once
#include "lvgl.h"

namespace ui::input::pager
{
#if defined(ARDUINO_T_LORA_PAGER) || defined(__linux__)
void install_help(lv_obj_t* root, lv_obj_t* title);
void prepare_group(lv_group_t* group);
bool handle_key(uint32_t key, bool pressed, lv_group_t* group);
#else
inline void install_help(lv_obj_t*, lv_obj_t*)
{
}
inline void prepare_group(lv_group_t*) {}
inline bool handle_key(uint32_t, bool, lv_group_t*) { return false; }
#endif
} // namespace ui::input::pager
