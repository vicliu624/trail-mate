#include "ui/screens/gps/gps_page_shell.h"

#include "ui/screens/common/page_shell_fallback.h"
#include "ui/screens/common/placeholder_page.h"
#include "ui/screens/gps/gps_page_runtime.h"

namespace
{

auto s_placeholder_state = ::ui::page_shell_fallback::make_unavailable_state("Map", "Map is unavailable on this target.");

} // namespace

namespace gps::ui::shell
{

void enter(void* user_data, lv_obj_t* parent)
{
    RouteSpec route{};
    route.host = static_cast<const Host*>(user_data);
    enter_route(&route, parent);
}

void exit(void* user_data, lv_obj_t* parent)
{
    (void)user_data;
    exit_route(nullptr, parent);
}

void enter_route(const RouteSpec* spec, lv_obj_t* parent)
{
    const RouteSpec route = spec ? *spec : RouteSpec{};
    if (!runtime::is_available())
    {
        s_placeholder_state.host = route.host;
        ::ui::placeholder_page::show(s_placeholder_state, parent);
        return;
    }

    runtime::enter(route.host, parent, route.projection, route.location, route.target);
    runtime::set_marker_binding(route.projection == Projection::Map ? route.markers : nullptr);
}

void exit_route(const RouteSpec* spec, lv_obj_t* parent)
{
    (void)spec;
    ::ui::page_shell_fallback::exit(
        s_placeholder_state,
        parent,
        runtime::is_available(),
        [](lv_obj_t* shell_parent)
        { runtime::exit(shell_parent); });
}

} // namespace gps::ui::shell
