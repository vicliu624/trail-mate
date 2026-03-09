#pragma once

#include "ui/screens/pc_link/pc_link_page_shell.h"

namespace pc_link::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace pc_link::ui::runtime
