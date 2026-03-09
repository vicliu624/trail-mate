#pragma once

#include "ui/screens/tracker/tracker_page_shell.h"

namespace tracker::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace tracker::ui::runtime
