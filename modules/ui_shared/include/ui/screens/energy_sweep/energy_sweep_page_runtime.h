#pragma once

#include "ui/screens/energy_sweep/energy_sweep_page_shell.h"

namespace energy_sweep::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace energy_sweep::ui::runtime
