#include "ui_lvgl_ux_packs/packs/t_display_p4_touch_ux_pack.h"

namespace ui_lvgl_ux
{
namespace
{

void build_page_manifest(ScreenRegistry& out)
{
    out.clear();
    (void)out.add({ScreenId::Calendar, "Agenda", true});
    (void)out.add({ScreenId::Dashboard, "Dashboard", true});
    (void)out.add({ScreenId::Chat, "Chat", true});
    (void)out.add({ScreenId::Contacts, "Contacts", true});
    (void)out.add({ScreenId::Map, "Map", true});
    (void)out.add({ScreenId::SkyPlot, "Sky Plot", true});
    (void)out.add({ScreenId::Gps, "GPS", true});
    (void)out.add({ScreenId::Team, "Team", true});
    (void)out.add({ScreenId::Tracker, "Tracker", true});
    (void)out.add({ScreenId::EnergySweep, "Protocol Probe", true});
    (void)out.add({ScreenId::WalkieTalkie, "Walkie", true});
    (void)out.add({ScreenId::Sstv, "SSTV", true});
    (void)out.add({ScreenId::Extensions, "Extensions", true});
    (void)out.add({ScreenId::Settings, "Settings", true});
}

bool manifest_contains(const ScreenRegistry& manifest, ScreenId id)
{
    const ScreenDescriptor* items = manifest.items();
    for (std::size_t index = 0; index < manifest.size(); ++index)
    {
        if (items[index].id == id && items[index].enabled)
        {
            return true;
        }
    }
    return false;
}

UxFeatureSet features_from_page_manifest()
{
    ScreenRegistry manifest;
    build_page_manifest(manifest);

    UxFeatureSet features{false, false, false, false, false,
                          false, false, false, false, false};
    features.chat = manifest_contains(manifest, ScreenId::Chat);
    features.contacts = manifest_contains(manifest, ScreenId::Contacts);
    features.map = manifest_contains(manifest, ScreenId::Map);
    features.gps = manifest_contains(manifest, ScreenId::Gps);
    features.team = manifest_contains(manifest, ScreenId::Team);
    features.tracker = manifest_contains(manifest, ScreenId::Tracker);
    features.settings = manifest_contains(manifest, ScreenId::Settings);
    features.walkie = manifest_contains(manifest, ScreenId::WalkieTalkie);
    features.sstv = manifest_contains(manifest, ScreenId::Sstv);
    features.extensions = manifest_contains(manifest, ScreenId::Extensions);
    return features;
}

} // namespace

const char* TDisplayP4TouchUxPack::id() const
{
    return "t_display_p4_touch";
}

const DeviceUxProfile& TDisplayP4TouchUxPack::profile() const
{
    static const DeviceUxProfile profile{
        "t_display_p4_touch",
        ScreenClass::TouchTablet,
        InputModel::Touch,
        MapMode::Full,
        ChatMode::Full,
        true,
        true,
        true,
        true,
    };
    return profile;
}

const UxFeatureSet& TDisplayP4TouchUxPack::features() const
{
    // UX features describe pages the pack can present. The runtime App Catalog
    // separately filters those pages using live platform capability checks.
    static const UxFeatureSet features = features_from_page_manifest();
    return features;
}

void TDisplayP4TouchUxPack::buildScreens(ScreenRegistry& out) const
{
    build_page_manifest(out);
}

void TDisplayP4TouchUxPack::buildInputBindings(InputBindingSet& out) const
{
    out.clear();
    (void)out.add({InputAction::Select, "Tap"});
    (void)out.add({InputAction::Back, "Bottom edge swipe"});
    (void)out.add({InputAction::Menu, "Menu gesture"});
    (void)out.add({InputAction::Compose, "Compose"});
    (void)out.add({InputAction::MapZoomIn, "Pinch zoom in"});
    (void)out.add({InputAction::MapZoomOut, "Pinch zoom out"});
}

} // namespace ui_lvgl_ux
