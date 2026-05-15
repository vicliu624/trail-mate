#include "ui_lvgl_ux_packs/runtime/lvgl_screen_host_adapter.h"

#include "ui_lvgl_ux_packs/runtime/compatibility_screen_factory.h"

namespace ui_lvgl_ux
{
namespace
{

void clearEntry(LvglScreenHostEntry& out)
{
    out.screen_id = ui::menu::MenuScreenId::Dashboard;
    out.binding_id = nullptr;
    out.available = false;
}

} // namespace

bool LvglScreenHostAdapter::resolve(ui::menu::MenuScreenId screen_id,
                                    LvglScreenHostEntry& out) const
{
    CompatibilityScreenFactory factory;
    CompatibilityScreenDescriptor descriptor;
    if (!factory.describe(screen_id, descriptor))
    {
        clearEntry(out);
        return false;
    }

    out.screen_id = descriptor.screen_id;
    out.binding_id = descriptor.binding_id;
    out.available = descriptor.available;
    return true;
}

} // namespace ui_lvgl_ux
