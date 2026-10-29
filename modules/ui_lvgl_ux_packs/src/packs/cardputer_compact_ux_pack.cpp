#include "ui_lvgl_ux_packs/packs/cardputer_compact_ux_pack.h"

namespace ui_lvgl_ux
{

const char* CardputerCompactUxPack::id() const
{
    return "cardputer_compact";
}

const DeviceUxProfile& CardputerCompactUxPack::profile() const
{
    static const DeviceUxProfile profile{
        "cardputer_compact",
        ScreenClass::CompactHandheld,
        InputModel::Keyboard,
        MapMode::Compact,
        ChatMode::QuickMessage,
        true,
        true,
        true,
        true,
    };
    return profile;
}

const UxFeatureSet& CardputerCompactUxPack::features() const
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
        false,
        true,
    };
    return features;
}

void CardputerCompactUxPack::buildScreens(ScreenRegistry& out) const
{
    out.clear();
    (void)out.add({ScreenId::Dashboard, "Dashboard", true});
    (void)out.add({ScreenId::Chat, "Chat", true});
    (void)out.add({ScreenId::Contacts, "Contacts", true});
    (void)out.add({ScreenId::Map, "Map", true});
    (void)out.add({ScreenId::SkyPlot, "Sky Plot", true});
    (void)out.add({ScreenId::Team, "Team", true});
    (void)out.add({ScreenId::Tracker, "Tracker", true});
    (void)out.add({ScreenId::Calculator, "Calculator", true});
    (void)out.add({ScreenId::Geocaching, "Geocaching", true});
    (void)out.add({ScreenId::EnergySweep, "Protocol Probe", true});
    (void)out.add({ScreenId::WalkieTalkie, "Walkie", true});
    (void)out.add({ScreenId::Extensions, "Extensions", true});
    (void)out.add({ScreenId::Settings, "Settings", true});
    (void)out.add({ScreenId::Calendar, "Agenda", true});
}

void CardputerCompactUxPack::buildInputBindings(InputBindingSet& out) const
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
    (void)out.add({InputAction::MapZoomIn, "Plus"});
    (void)out.add({InputAction::MapZoomOut, "Minus"});
}

} // namespace ui_lvgl_ux
