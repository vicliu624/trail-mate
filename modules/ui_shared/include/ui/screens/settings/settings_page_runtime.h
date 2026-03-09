#pragma once

#include "ui/screens/settings/settings_page_shell.h"

namespace settings::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace settings::ui::runtime
