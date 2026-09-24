#include "ui_lvgl_ux_packs/packs/deck_touch_ux_pack.h"
#include "ui_presentation/page/page_manifest.h"

namespace ui_lvgl_ux
{
namespace
{
using ui::presentation::PageId;

const ui::presentation::PageManifest* manifest()
{
    return ui::presentation::findPageManifest("deck_full_manifest");
}

bool hasPage(PageId id)
{
    const auto* pages = manifest();
    if (!pages) return false;
    for (std::size_t index = 0; index < pages->item_count; ++index)
        if (pages->items[index].page_id == id && pages->items[index].enabled) return true;
    return false;
}

bool screenId(PageId id, ScreenId& screen)
{
    switch (id)
    {
    case PageId::Calendar:
        screen = ScreenId::Calendar;
        return true;
    case PageId::Dashboard:
        screen = ScreenId::Dashboard;
        return true;
    case PageId::Chat:
        screen = ScreenId::Chat;
        return true;
    case PageId::Contacts:
        screen = ScreenId::Contacts;
        return true;
    case PageId::Map:
        screen = ScreenId::Map;
        return true;
    case PageId::Gps:
        screen = ScreenId::Gps;
        return true;
    case PageId::SkyPlot:
        screen = ScreenId::SkyPlot;
        return true;
    case PageId::Team:
        screen = ScreenId::Team;
        return true;
    case PageId::Tracker:
        screen = ScreenId::Tracker;
        return true;
    case PageId::EnergySweep:
        screen = ScreenId::EnergySweep;
        return true;
    case PageId::Settings:
        screen = ScreenId::Settings;
        return true;
    case PageId::WalkieTalkie:
        screen = ScreenId::WalkieTalkie;
        return true;
    case PageId::Sstv:
        screen = ScreenId::Sstv;
        return true;
    case PageId::Extensions:
        screen = ScreenId::Extensions;
        return true;
    default:
        return false;
    }
}
} // namespace

const char* DeckTouchUxPack::id() const { return "deck_touch"; }

const DeviceUxProfile& DeckTouchUxPack::profile() const
{
    static const DeviceUxProfile value{
        "deck_touch", ScreenClass::DeckLandscape, InputModel::Touch,
        MapMode::Full, ChatMode::Full, true, true, true, true};
    return value;
}

const UxFeatureSet& DeckTouchUxPack::features() const
{
    static const UxFeatureSet value{
        hasPage(PageId::Chat), hasPage(PageId::Contacts), hasPage(PageId::Map),
        hasPage(PageId::Gps), hasPage(PageId::Team), hasPage(PageId::Tracker),
        hasPage(PageId::Settings), hasPage(PageId::WalkieTalkie),
        hasPage(PageId::Sstv), hasPage(PageId::Extensions)};
    return value;
}

void DeckTouchUxPack::buildScreens(ScreenRegistry& out) const
{
    out.clear();
    const auto* pages = manifest();
    if (!pages) return;
    for (std::size_t index = 0; index < pages->item_count; ++index)
    {
        const auto& page = pages->items[index];
        ScreenId screen{};
        if (page.enabled && page.visible_in_menu && screenId(page.page_id, screen))
            if (!out.add({screen, page.binding_id, true})) return;
    }
}

void DeckTouchUxPack::buildInputBindings(InputBindingSet& out) const
{
    out.clear();
    (void)out.add({InputAction::Select, "Tap"});
    (void)out.add({InputAction::Back, "Back button"});
    (void)out.add({InputAction::Menu, "Home button"});
    (void)out.add({InputAction::Compose, "On-screen keyboard"});
    (void)out.add({InputAction::MapZoomIn, "On-screen zoom in"});
    (void)out.add({InputAction::MapZoomOut, "On-screen zoom out"});
}
} // namespace ui_lvgl_ux
