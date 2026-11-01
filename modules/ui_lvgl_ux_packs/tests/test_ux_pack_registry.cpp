#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include <cassert>
#include <cstring>

namespace
{

bool contains(const ui_lvgl_ux::ScreenRegistry& registry,
              ui_lvgl_ux::ScreenId screen_id)
{
    const auto* items = registry.items();
    for (std::size_t index = 0; index < registry.size(); ++index)
    {
        if (items[index].id == screen_id && items[index].enabled)
        {
            return true;
        }
    }
    return false;
}

} // namespace

int main()
{
    const ui_lvgl_ux::IUxPack* compatibility =
        ui_lvgl_ux::findUxPackById("compatibility");
    const ui_lvgl_ux::IUxPack* uconsole =
        ui_lvgl_ux::findUxPackById("uconsole_desktop");
    const ui_lvgl_ux::IUxPack* tiny =
        ui_lvgl_ux::findUxPackById("tiny_node_status");
    const ui_lvgl_ux::IUxPack* cardputer =
        ui_lvgl_ux::findUxPackById("cardputer_compact");
    const ui_lvgl_ux::IUxPack* simulator =
        ui_lvgl_ux::findUxPackById("simulator_full");
    const ui_lvgl_ux::IUxPack* t_display_p4 =
        ui_lvgl_ux::findUxPackById("t_display_p4_touch");

    assert(compatibility != nullptr);
    assert(uconsole != nullptr);
    assert(tiny != nullptr);
    assert(cardputer != nullptr);
    assert(simulator != nullptr);
    assert(t_display_p4 != nullptr);
    assert(std::strcmp(compatibility->id(), "compatibility") == 0);
    assert(std::strcmp(uconsole->id(), "uconsole_desktop") == 0);
    assert(std::strcmp(tiny->id(), "tiny_node_status") == 0);
    assert(std::strcmp(cardputer->id(), "cardputer_compact") == 0);
    assert(std::strcmp(simulator->id(), "simulator_full") == 0);
    assert(std::strcmp(t_display_p4->id(), "t_display_p4_touch") == 0);
    assert(ui_lvgl_ux::findUxPackById("missing") == nullptr);
    assert(ui_lvgl_ux::findUxPackById(nullptr) == nullptr);

    ui_lvgl_ux::ScreenRegistry screens;
    uconsole->buildScreens(screens);
    assert(screens.size() == 12);
    assert(contains(screens, ui_lvgl_ux::ScreenId::Dashboard));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Chat));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Contacts));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Map));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Gps));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Team));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Tracker));
    assert(contains(screens, ui_lvgl_ux::ScreenId::EnergySweep));
    assert(contains(screens, ui_lvgl_ux::ScreenId::WalkieTalkie));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Sstv));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Extensions));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Settings));

    t_display_p4->buildScreens(screens);
    assert(screens.size() == 14);
    assert(contains(screens, ui_lvgl_ux::ScreenId::Calendar));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Dashboard));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Chat));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Contacts));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Map));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Team));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Tracker));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Settings));
    return 0;
}
