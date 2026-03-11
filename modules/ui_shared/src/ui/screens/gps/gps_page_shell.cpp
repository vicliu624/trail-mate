#include "ui/screens/gps/gps_page_shell.h"

#include "ui/screens/common/page_shell_fallback.h"
#include "ui/screens/gps/gps_page_runtime.h"

namespace
{

auto s_placeholder_state = ::ui::page_shell_fallback::make_unavailable_state("Map", "Map is unavailable on this target.");

} // namespace

namespace gps::ui::shell
{

void enter(void* user_data, lv_obj_t* parent)
{
    ::ui::page_shell_fallback::enter<Host>(
        s_placeholder_state,
        user_data,
        parent,
        runtime::is_available(),
        [](const Host* host, lv_obj_t* shell_parent)
        { runtime::enter(host, shell_parent); });
}

void exit(void* user_data, lv_obj_t* parent)
{
    (void)user_data;
    ::ui::page_shell_fallback::exit(
        s_placeholder_state,
        parent,
        runtime::is_available(),
        [](lv_obj_t* shell_parent)
        { runtime::exit(shell_parent); });
}

} // namespace gps::ui::shell
