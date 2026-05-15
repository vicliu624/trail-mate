#include "ui_ascii_runtime/ascii_menu_runtime_adapter.h"

namespace trailmate::linux_sim
{

bool AsciiMenuRuntimeAdapter::buildLines(const ui::menu::MenuModel& menu,
                                         AsciiMenuLine* out,
                                         std::size_t capacity,
                                         std::size_t& out_count) const
{
    out_count = 0;

    if (menu.size() == 0)
    {
        return true;
    }

    if (out == nullptr)
    {
        return false;
    }

    for (std::size_t index = 0; index < menu.size(); ++index)
    {
        const ui::menu::MenuItem& item = menu.items()[index];
        if (!item.enabled)
        {
            continue;
        }

        if (out_count >= capacity)
        {
            return false;
        }

        out[out_count].screen_id = item.screen_id;
        out[out_count].route = ui::screen::routeForMenuScreen(item.screen_id);
        out[out_count].label = item.label;
        out[out_count].enabled = item.enabled;
        ++out_count;
    }

    return true;
}

} // namespace trailmate::linux_sim
