#pragma once

#include "ui/screens/usb/usb_page_shell.h"

namespace usb_storage::ui::runtime
{

bool is_available();
void enter(const shell::Host* host, lv_obj_t* parent);
void exit(lv_obj_t* parent);

} // namespace usb_storage::ui::runtime
