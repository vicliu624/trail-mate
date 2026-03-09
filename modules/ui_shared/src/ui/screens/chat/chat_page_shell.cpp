#include "ui/screens/chat/chat_page_shell.h"

#include "ui/screens/chat/chat_page_runtime.h"

namespace chat::ui::shell
{

lv_obj_t* get_container()
{
    return runtime::get_container();
}

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

} // namespace chat::ui::shell
