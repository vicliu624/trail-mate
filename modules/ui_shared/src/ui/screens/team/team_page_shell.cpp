#include "ui/screens/team/team_page_shell.h"

#include "ui/screens/team/team_page_runtime.h"

namespace team::ui::shell
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

bool handle_event(void* user_data, sys::Event* event)
{
    (void)user_data;
    return runtime::handle_event(event);
}

} // namespace team::ui::shell
