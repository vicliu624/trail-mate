#include "ui_ascii_runtime/ascii_screen_host_adapter.h"

#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"

namespace trailmate::linux_sim
{
namespace
{

void clearDescriptor(AsciiScreenDescriptor& out)
{
    out.screen_id = ui::menu::MenuScreenId::Dashboard;
    out.binding_id = nullptr;
    out.available = false;
}

} // namespace

bool AsciiScreenHostAdapter::resolve(const ui::screen::ScreenRoute& route,
                                     AsciiScreenDescriptor& out) const
{
    if (!route.valid)
    {
        clearDescriptor(out);
        return false;
    }

    ui_lvgl_ux::CompatibilityScreenFactory factory;
    ui_lvgl_ux::CompatibilityScreenDescriptor descriptor;
    if (!factory.describe(route.screen_id, descriptor))
    {
        clearDescriptor(out);
        return false;
    }

    out.screen_id = descriptor.screen_id;
    out.binding_id = descriptor.binding_id;
    out.available = descriptor.available;
    return true;
}

} // namespace trailmate::linux_sim
