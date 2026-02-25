#pragma once

#if defined(ARDUINO_M5STACK_TAB5)

#include "lvgl.h"

namespace ui::tab5
{

// Create a bottom virtual navigation bar for Tab5 and attach it to the given root.
// This provides Back / Up / Down / OK keys for touch-only operation.
void init_virtual_keys(lv_obj_t* root);

} // namespace ui::tab5

#endif // ARDUINO_M5STACK_TAB5

