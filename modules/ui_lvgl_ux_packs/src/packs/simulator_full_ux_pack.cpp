#include "ui_lvgl_ux_packs/packs/simulator_full_ux_pack.h"

namespace ui_lvgl_ux
{

const char* SimulatorFullUxPack::id() const
{
    return "simulator_full";
}

const DeviceUxProfile& SimulatorFullUxPack::profile() const
{
    static const DeviceUxProfile profile{
        "simulator_full",
        ScreenClass::Desktop,
        InputModel::DesktopKeyboardMouse,
        MapMode::Full,
        ChatMode::Full,
        true,
        true,
        true,
        true,
    };
    return profile;
}

const UxFeatureSet& SimulatorFullUxPack::features() const
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

void SimulatorFullUxPack::buildScreens(ScreenRegistry& out) const
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

void SimulatorFullUxPack::buildInputBindings(InputBindingSet& out) const
{
    out.clear();
    (void)out.add({InputAction::Up, "Keyboard up"});
    (void)out.add({InputAction::Down, "Keyboard down"});
    (void)out.add({InputAction::Left, "Keyboard left"});
    (void)out.add({InputAction::Right, "Keyboard right"});
    (void)out.add({InputAction::Select, "Enter"});
    (void)out.add({InputAction::Back, "Escape"});
    (void)out.add({InputAction::Menu, "Menu"});
    (void)out.add({InputAction::Compose, "Compose"});
    (void)out.add({InputAction::MapZoomIn, "Zoom in"});
    (void)out.add({InputAction::MapZoomOut, "Zoom out"});
}

} // namespace ui_lvgl_ux
