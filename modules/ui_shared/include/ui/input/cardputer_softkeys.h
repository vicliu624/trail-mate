#pragma once
#include "lvgl.h"

namespace ui::input::cardputer::softkeys
{
// Slot zero is the physical 4 key; slot four is the physical 8 key.
// The keyboard extends beyond the LCD on the left. At 320 pixels its key
// centres are 48, 108, 168, 228 and 288, rather than five equal screen cells.
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
void install_row(lv_obj_t* bar);
void bind_button(lv_obj_t* bar, lv_obj_t* button, int slot = -1);
bool activate(uint32_t key, lv_group_t* group);
#else
inline void install_row(lv_obj_t*)
{
}
inline void bind_button(lv_obj_t*, lv_obj_t*, int = -1) {}
#endif
} // namespace ui::input::cardputer::softkeys
