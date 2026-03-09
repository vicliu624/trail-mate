#pragma once

#include "ui/screens/contacts/contacts_page_shell.h"

namespace contacts::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace contacts::ui::runtime
