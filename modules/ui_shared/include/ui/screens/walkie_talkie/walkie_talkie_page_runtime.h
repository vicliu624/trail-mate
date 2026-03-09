#pragma once

#include "ui/screens/walkie_talkie/walkie_talkie_page_shell.h"

namespace walkie_page::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace walkie_page::ui::runtime
