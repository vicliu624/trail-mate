#include "ui_presentation/page/page_manifest.h"

#include <cstring>

namespace ui::presentation
{
namespace
{

constexpr PageManifestItem kTab5TouchItems[] = {
    {PageId::Dashboard, "dashboard", true, true},
    {PageId::Map, "map", true, true},
    {PageId::Gps, "gps", true, true},
    {PageId::Chat, "chat", true, true},
    {PageId::Team, "team", true, true},
    {PageId::Tracker, "tracker", true, true},
    {PageId::Contacts, "contacts", true, true},
    {PageId::Settings, "settings", true, true},
};

constexpr PageManifestItem kTDisplayP4TouchItems[] = {
    {PageId::Dashboard, "dashboard", true, true},
    {PageId::Map, "map", true, true},
    {PageId::Gps, "gps", true, true},
    {PageId::Chat, "chat", true, true},
    {PageId::Team, "team", true, true},
    {PageId::Tracker, "tracker", true, true},
    {PageId::Settings, "settings", true, true},
};

constexpr PageManifestItem kPagerCompactItems[] = {
    {PageId::Dashboard, "dashboard", true, true},
    {PageId::Chat, "chat", true, true},
    {PageId::Gps, "gps", true, true},
    {PageId::Map, "map", true, true},
    {PageId::Team, "team", true, true},
    {PageId::Settings, "settings", true, true},
};

constexpr PageManifestItem kDeckFullItems[] = {
    {PageId::Dashboard, "dashboard", true, true},
    {PageId::Chat, "chat", true, true},
    {PageId::Team, "team", true, true},
    {PageId::Map, "map", true, true},
    {PageId::Gps, "gps", true, true},
    {PageId::Tracker, "tracker", true, true},
    {PageId::Contacts, "contacts", true, true},
    {PageId::Extensions, "extensions", true, true},
    {PageId::Settings, "settings", true, true},
};

constexpr PageManifestItem kWatchCompactItems[] = {
    {PageId::Dashboard, "dashboard", true, true},
    {PageId::Chat, "chat", true, true},
    {PageId::Gps, "gps", true, true},
    {PageId::Team, "team", true, true},
    {PageId::Settings, "settings", true, true},
};

constexpr PageManifestItem kUConsoleDesktopItems[] = {
    {PageId::Dashboard, "dashboard", true, true},
    {PageId::Chat, "chat", true, true},
    {PageId::Map, "map", true, true},
    {PageId::Team, "team", true, true},
    {PageId::Gps, "gps", true, true},
    {PageId::Tracker, "tracker", true, true},
    {PageId::Settings, "settings", true, true},
    {PageId::Diagnostics, "diagnostics", true, true},
};

constexpr PageManifestItem kCardputerCompactItems[] = {
    {PageId::Dashboard, "dashboard", true, true},
    {PageId::Chat, "chat", true, true},
    {PageId::Map, "map", true, true},
    {PageId::Gps, "gps", true, true},
    {PageId::Team, "team", true, true},
    {PageId::Settings, "settings", true, true},
};

constexpr PageManifestItem kNodeHeadlessItems[] = {
    {PageId::NodeStatus, "node_status", true, false},
    {PageId::Diagnostics, "diagnostics", true, false},
};

constexpr PageManifest kManifests[] = {
    {"tab5_touch_manifest", kTab5TouchItems, sizeof(kTab5TouchItems) / sizeof(kTab5TouchItems[0])},
    {"tdisplayp4_touch_manifest", kTDisplayP4TouchItems, sizeof(kTDisplayP4TouchItems) / sizeof(kTDisplayP4TouchItems[0])},
    {"pager_compact_manifest", kPagerCompactItems, sizeof(kPagerCompactItems) / sizeof(kPagerCompactItems[0])},
    {"deck_full_manifest", kDeckFullItems, sizeof(kDeckFullItems) / sizeof(kDeckFullItems[0])},
    {"watch_compact_manifest", kWatchCompactItems, sizeof(kWatchCompactItems) / sizeof(kWatchCompactItems[0])},
    {"uconsole_desktop_manifest", kUConsoleDesktopItems, sizeof(kUConsoleDesktopItems) / sizeof(kUConsoleDesktopItems[0])},
    {"cardputer_compact_manifest", kCardputerCompactItems, sizeof(kCardputerCompactItems) / sizeof(kCardputerCompactItems[0])},
    {"node_headless_manifest", kNodeHeadlessItems, sizeof(kNodeHeadlessItems) / sizeof(kNodeHeadlessItems[0])},
};

} // namespace

const PageManifest* findPageManifest(const char* manifest_id)
{
    if (manifest_id == nullptr)
    {
        return nullptr;
    }

    for (const auto& manifest : kManifests)
    {
        if (std::strcmp(manifest.manifest_id, manifest_id) == 0)
        {
            return &manifest;
        }
    }
    return nullptr;
}

} // namespace ui::presentation
