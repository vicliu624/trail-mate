#include "ui/screens/extensions/extensions_page_shell.h"

#include "ui/screens/extensions/extensions_page_runtime.h"

namespace extensions::ui::shell
{

void enter(void* user_data, lv_obj_t* parent)
{
    const Host* host = static_cast<const Host*>(user_data);
    runtime::enter(host, parent);
}

void exit(void* user_data, lv_obj_t* parent)
{
    (void)user_data;
    runtime::exit(parent);
}

} // namespace extensions::ui::shell
