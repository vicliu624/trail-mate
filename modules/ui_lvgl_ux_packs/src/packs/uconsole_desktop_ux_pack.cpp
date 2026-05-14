#include "ui_lvgl_ux_packs/packs/uconsole_desktop_ux_pack.h"

namespace ui_lvgl_ux
{

const char* UConsoleDesktopUxPack::id() const
{
    return "uconsole_desktop";
}

const DeviceUxProfile& UConsoleDesktopUxPack::profile() const
{
    static const DeviceUxProfile profile{
        "uconsole_desktop",
        ScreenClass::Desktop,
        InputModel::DesktopKeyboardMouse,
        MapMode::Desktop,
        ChatMode::Full,
        true,
        true,
        true,
        true,
    };
    return profile;
}

const UxFeatureSet& UConsoleDesktopUxPack::features() const
{
    static const UxFeatureSet features{
        true,
        false,
        true,
        true,
        true,
        true,
        true,
        false,
        false,
        false,
    };
    return features;
}

void UConsoleDesktopUxPack::buildScreens(ScreenRegistry& out) const
{
    out.clear();
    (void)out.add({ScreenId::Dashboard, "Dashboard", true});
    (void)out.add({ScreenId::Chat, "Chat", true});
    (void)out.add({ScreenId::Map, "Map", true});
    (void)out.add({ScreenId::Gps, "GPS", true});
    (void)out.add({ScreenId::Team, "Team", true});
    (void)out.add({ScreenId::Tracker, "Tracker", true});
    (void)out.add({ScreenId::Settings, "Settings", true});
}

void UConsoleDesktopUxPack::buildInputBindings(InputBindingSet& out) const
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
