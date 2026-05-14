#include "ui_lvgl_ux_packs/packs/compatibility_ux_pack.h"

namespace ui_lvgl_ux
{

const char* CompatibilityUxPack::id() const
{
    return "compatibility";
}

const DeviceUxProfile& CompatibilityUxPack::profile() const
{
    static const DeviceUxProfile profile{
        "compatibility",
        ScreenClass::CompactHandheld,
        InputModel::ButtonsOnly,
        MapMode::Compact,
        ChatMode::QuickMessage,
        true,
        true,
        true,
        true,
    };
    return profile;
}

const UxFeatureSet& CompatibilityUxPack::features() const
{
    static const UxFeatureSet features{
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
    };
    return features;
}

void CompatibilityUxPack::buildScreens(ScreenRegistry& out) const
{
    out.clear();
    (void)out.add({ScreenId::Dashboard, "Dashboard", true});
    (void)out.add({ScreenId::Chat, "Chat", true});
    (void)out.add({ScreenId::Contacts, "Contacts", true});
    (void)out.add({ScreenId::Map, "Map", true});
    (void)out.add({ScreenId::Gps, "GPS", true});
    (void)out.add({ScreenId::Team, "Team", true});
    (void)out.add({ScreenId::Tracker, "Tracker", true});
    (void)out.add({ScreenId::Settings, "Settings", true});
    (void)out.add({ScreenId::WalkieTalkie, "Walkie", true});
    (void)out.add({ScreenId::Sstv, "SSTV", true});
    (void)out.add({ScreenId::Extensions, "Extensions", true});
}

void CompatibilityUxPack::buildInputBindings(InputBindingSet& out) const
{
    out.clear();
    (void)out.add({InputAction::Up, "Up"});
    (void)out.add({InputAction::Down, "Down"});
    (void)out.add({InputAction::Left, "Left"});
    (void)out.add({InputAction::Right, "Right"});
    (void)out.add({InputAction::Select, "Select"});
    (void)out.add({InputAction::Back, "Back"});
    (void)out.add({InputAction::Menu, "Menu"});
    (void)out.add({InputAction::Compose, "Compose"});
    (void)out.add({InputAction::MapZoomIn, "Map zoom in"});
    (void)out.add({InputAction::MapZoomOut, "Map zoom out"});
}

} // namespace ui_lvgl_ux
