#pragma once

#include "ui/screens/chat/chat_page_shell.h"

namespace chat::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);
lv_obj_t* get_container();

} // namespace chat::ui::runtime
