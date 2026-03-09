#pragma once

#include "ui/screens/sstv/sstv_page_shell.h"

namespace sstv_page::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace sstv_page::ui::runtime
