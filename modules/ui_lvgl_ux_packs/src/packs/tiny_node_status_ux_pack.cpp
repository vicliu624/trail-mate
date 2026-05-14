#include "ui_lvgl_ux_packs/packs/tiny_node_status_ux_pack.h"

namespace ui_lvgl_ux
{

const char* TinyNodeStatusUxPack::id() const
{
    return "tiny_node_status";
}

const DeviceUxProfile& TinyNodeStatusUxPack::profile() const
{
    static const DeviceUxProfile profile{
        "tiny_node_status",
        ScreenClass::TinyStatus,
        InputModel::ButtonsOnly,
        MapMode::None,
        ChatMode::StatusOnly,
        true,
        true,
        false,
        false,
    };
    return profile;
}

const UxFeatureSet& TinyNodeStatusUxPack::features() const
{
    static const UxFeatureSet features{
        false,
        false,
        false,
        true,
        true,
        false,
        true,
        false,
        false,
        false,
    };
    return features;
}

void TinyNodeStatusUxPack::buildScreens(ScreenRegistry& out) const
{
    out.clear();
    (void)out.add({ScreenId::Dashboard, "Status", true});
    (void)out.add({ScreenId::Team, "Team", true});
    (void)out.add({ScreenId::Gps, "GPS", true});
    (void)out.add({ScreenId::Settings, "Settings", true});
}

void TinyNodeStatusUxPack::buildInputBindings(InputBindingSet& out) const
{
    out.clear();
    (void)out.add({InputAction::Up, "Previous"});
    (void)out.add({InputAction::Down, "Next"});
    (void)out.add({InputAction::Select, "Select"});
    (void)out.add({InputAction::Back, "Back"});
    (void)out.add({InputAction::Menu, "Menu"});
}

} // namespace ui_lvgl_ux
