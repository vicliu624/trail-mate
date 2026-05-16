#pragma once

#include <cstddef>

namespace ui::presentation
{

enum class PageId
{
    Dashboard,
    Chat,
    Map,
    Gps,
    Contacts,
    Team,
    Tracker,
    Settings,
    WalkieTalkie,
    Sstv,
    Extensions,
    NodeStatus,
    Diagnostics,
};

struct PageManifestItem
{
    PageId page_id;
    const char* binding_id;
    bool enabled;
    bool visible_in_menu;
};

struct PageManifest
{
    const char* manifest_id;
    const PageManifestItem* items;
    std::size_t item_count;
};

const PageManifest* findPageManifest(const char* manifest_id);

} // namespace ui::presentation
