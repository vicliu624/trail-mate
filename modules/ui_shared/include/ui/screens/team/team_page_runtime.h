#pragma once

#include "ui/screens/team/team_page_shell.h"

namespace team::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);
bool handle_event(sys::Event* event);

} // namespace team::ui::runtime
