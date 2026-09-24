#include "ui/screens/agenda/agenda_page_shell.h"
#include "ui/screens/agenda/agenda_page_runtime.h"

namespace ui::agenda::page
{
void enter(void* user_data, lv_obj_t* parent) { runtime::enter(static_cast<const Host*>(user_data), parent); }
void exit(void*, lv_obj_t*) { runtime::exit(); }
} // namespace ui::agenda::page
