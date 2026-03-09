#pragma once

#include "ui/screens/gnss/gnss_skyplot_page_shell.h"

namespace gnss::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace gnss::ui::runtime
