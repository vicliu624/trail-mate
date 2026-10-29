#include "ui_lvgl_ux_packs/packs/t_display_p4_touch_ux_pack.h"

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

const ui_lvgl_ux::InputBinding* find_input(const ui_lvgl_ux::InputBindingSet& bindings,
                                           ui_lvgl_ux::InputAction action)
{
    const auto* items = bindings.items();
    for (std::size_t index = 0; index < bindings.size(); ++index)
    {
        if (items[index].action == action)
        {
            return &items[index];
        }
    }
    return nullptr;
}

} // namespace

int main()
{
    ui_lvgl_ux::TDisplayP4TouchUxPack pack;
    assert(std::strcmp(pack.id(), "t_display_p4_touch") == 0);

    const auto& profile = pack.profile();
    assert(profile.screen_class == ui_lvgl_ux::ScreenClass::TouchTablet);
    assert(profile.input_model == ui_lvgl_ux::InputModel::Touch);
    assert(profile.map_mode == ui_lvgl_ux::MapMode::Full);
    assert(profile.chat_mode == ui_lvgl_ux::ChatMode::Full);
    assert(profile.supports_team_actions);
    assert(profile.supports_gps_page);
    assert(profile.supports_key_verification_modal);
    assert(profile.supports_position_picker);

    const auto& features = pack.features();
    assert(features.chat);
    assert(features.contacts);
    assert(features.map);
    assert(features.gps);
    assert(features.team);
    assert(features.tracker);
    assert(features.settings);
    assert(features.walkie);
    assert(features.sstv);
    assert(features.extensions);

    ui_lvgl_ux::ScreenRegistry screens;
    pack.buildScreens(screens);
    assert(screens.size() == 14);
    assert(contains(screens, ui_lvgl_ux::ScreenId::Calendar));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Dashboard));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Chat));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Contacts));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Map));
    assert(contains(screens, ui_lvgl_ux::ScreenId::SkyPlot));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Gps));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Team));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Tracker));
    assert(contains(screens, ui_lvgl_ux::ScreenId::EnergySweep));
    assert(contains(screens, ui_lvgl_ux::ScreenId::WalkieTalkie));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Sstv));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Extensions));
    assert(contains(screens, ui_lvgl_ux::ScreenId::Settings));

    ui_lvgl_ux::InputBindingSet inputs;
    pack.buildInputBindings(inputs);
    assert(inputs.size() == 6);
    const auto* back = find_input(inputs, ui_lvgl_ux::InputAction::Back);
    assert(back != nullptr);
    assert(std::strcmp(back->hint, "Bottom edge swipe") == 0);
    return 0;
}
