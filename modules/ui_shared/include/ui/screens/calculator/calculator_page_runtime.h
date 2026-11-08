#pragma once

#include "lvgl.h"
#include "ui/page/page_host.h"

namespace calculator::ui::runtime
{

void enter(const ::ui::page::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);
// Explicit keyboard control mode shares the button's registered action without
// synthesizing the keypad release click that expression mode suppresses.
bool activate_control(lv_obj_t* button);

} // namespace calculator::ui::runtime
