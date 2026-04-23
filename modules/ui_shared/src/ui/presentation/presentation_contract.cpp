#include "ui/presentation/presentation_contract.h"

#include <cstring>

namespace ui::presentation
{
namespace
{

constexpr PageArchetypeInfo kPageArchetypes[] = {
    {"page_archetype.menu_dashboard", "Launcher and dashboard-style home surface"},
    {"page_archetype.boot_splash", "Startup splash and early boot diagnostics"},
    {"page_archetype.watch_face", "Low-power glanceable standby surface"},
    {"page_archetype.chat_list", "Conversation list and unread summary"},
    {"page_archetype.chat_conversation", "Threaded message view with action bar"},
    {"page_archetype.chat_compose", "Message composition and IME-driven input"},
    {"page_archetype.directory_browser",
     "Selector-driven directory flow such as settings, contacts, and extensions"},
    {"page_archetype.map_focus", "Map-centric pages with overlays and controls"},
    {"page_archetype.instrument_panel", "Telemetry and instrument visualization pages"},
    {"page_archetype.service_panel", "Connection, transfer, and service utility pages"},
};

constexpr RegionInfo kRegions[] = {
    {"region.chrome.top_bar", "Shared header chrome"},
    {"region.chrome.status_row", "Shared status icon and badge row"},
    {"region.chrome.footer", "Shared footer or secondary chrome strip"},
    {"region.menu.app_grid", "Primary launcher grid or list"},
    {"region.menu.bottom_chips", "Bottom diagnostic chips and summaries"},
    {"region.boot.hero", "Boot logo or hero illustration area"},
    {"region.boot.log", "Boot log and progress output"},
    {"region.watchface.primary", "Standby clock and glanceable metrics"},
    {"region.chat.thread", "Conversation list or message thread body"},
    {"region.chat.composer", "Message composer and IME host zone"},
    {"region.chat.action_bar", "Message actions and thread-level shortcuts"},
    {"region.directory.header", "Directory title and contextual controls"},
    {"region.directory.selector", "Primary selector or scope-switching region"},
    {"region.directory.content", "Directory entry and detail content region"},
    {"region.map.viewport", "Primary map or geographic canvas"},
    {"region.map.overlay.primary", "Primary overlay controls and metrics"},
    {"region.map.overlay.secondary", "Secondary overlay chips and callouts"},
    {"region.instrument.canvas", "Primary instrument visualization"},
    {"region.instrument.legend", "Legend, metrics, and secondary instrument data"},
    {"region.service.primary_panel", "Main service content panel"},
    {"region.service.footer_actions", "Service page bottom actions"},
};

constexpr ComponentInfo kComponents[] = {
    {"component.top_bar.container", "Top bar outer container"},
    {"component.top_bar.back_button", "Back button within the top bar"},
    {"component.top_bar.title_label", "Primary top bar title text"},
    {"component.top_bar.right_label", "Right-aligned top bar meta label"},
    {"component.status.icon_strip", "Shared status icon strip"},
    {"component.menu.app_button", "Launcher app entry button"},
    {"component.menu.bottom_chip", "Bottom diagnostic chip"},
    {"component.modal.panel", "Modal container panel"},
    {"component.notification.banner", "System notification banner"},
    {"component.toast.banner", "Toast surface"},
    {"component.directory.selector_button", "Directory selector or category button"},
    {"component.directory.entry_item", "Directory content entry"},
    {"component.info_card.base", "Shared information card"},
    {"component.chat.list_item", "Conversation list row"},
    {"component.chat.bubble.self", "Message bubble for local sender"},
    {"component.chat.bubble.peer", "Message bubble for remote sender"},
    {"component.chat.composer.editor", "Text editor inside compose page"},
    {"component.action_button.primary", "Primary action button"},
    {"component.action_button.secondary", "Secondary action button"},
    {"component.status_chip", "Compact status or mode chip"},
};

constexpr BindingInfo kBindings[] = {
    {"binding.screen.title", "Screen title string", false},
    {"binding.screen.subtitle", "Screen subtitle or meta string", false},
    {"binding.status.summary", "Status summary text or metrics", false},
    {"binding.menu.app_entries", "Menu application entries", true},
    {"binding.menu.bottom_summary", "Menu bottom diagnostic summary", false},
    {"binding.boot.log_lines", "Boot log lines", true},
    {"binding.chat.thread_items", "Chat rows or message thread items", true},
    {"binding.chat.draft_text", "Editable draft text", false},
    {"binding.directory.selectors", "Directory selector entries", true},
    {"binding.directory.entries", "Directory content entries", true},
    {"binding.directory.detail_document", "Selected item detail document", false},
    {"binding.map.viewport_model", "Map viewport data model", false},
    {"binding.map.overlay_summary", "Map overlay summary data", false},
    {"binding.instrument.primary_series", "Instrument primary data series", true},
    {"binding.instrument.legend_items", "Instrument legend items", true},
    {"binding.service.body_text", "Service page primary body text", false},
    {"binding.package_catalog_entries", "Package catalog entries", true},
    {"binding.settings.section_entries", "Settings sections and rows", true},
};

constexpr ActionInfo kActions[] = {
    {"action.navigate.back", "Navigate back to the previous screen", true},
    {"action.navigate.open", "Open the currently focused item", true},
    {"action.navigate.next", "Move focus or selection forward", false},
    {"action.navigate.previous", "Move focus or selection backward", false},
    {"action.activate.primary", "Trigger the page primary action", true},
    {"action.activate.secondary", "Trigger the page secondary action", true},
    {"action.refresh.content", "Refresh the bound content source", true},
    {"action.package.install", "Install the selected package", true},
    {"action.package.remove", "Remove the selected package", true},
    {"action.presentation.apply", "Apply the selected theme or presentation", true},
    {"action.message.send", "Send the current draft message", true},
    {"action.directory.toggle_selector", "Toggle the directory selector region", true},
    {"action.map.zoom_in", "Zoom the map in", true},
    {"action.map.zoom_out", "Zoom the map out", true},
};

struct RegionMembership
{
    PageArchetype archetype;
    RegionSlot region;
    bool required;
    bool repeatable;
};

constexpr RegionMembership kRegionMemberships[] = {
    {PageArchetype::MenuDashboard, RegionSlot::ChromeTopBar, true, false},
    {PageArchetype::MenuDashboard, RegionSlot::ChromeStatusRow, false, false},
    {PageArchetype::MenuDashboard, RegionSlot::MenuAppGrid, true, false},
    {PageArchetype::MenuDashboard, RegionSlot::MenuBottomChips, false, true},

    {PageArchetype::BootSplash, RegionSlot::BootHero, true, false},
    {PageArchetype::BootSplash, RegionSlot::BootLog, true, true},

    {PageArchetype::WatchFace, RegionSlot::WatchFacePrimary, true, false},
    {PageArchetype::WatchFace, RegionSlot::ChromeFooter, false, false},

    {PageArchetype::ChatList, RegionSlot::ChromeTopBar, true, false},
    {PageArchetype::ChatList, RegionSlot::ChatThread, true, true},
    {PageArchetype::ChatList, RegionSlot::ChromeFooter, false, false},

    {PageArchetype::ChatConversation, RegionSlot::ChromeTopBar, true, false},
    {PageArchetype::ChatConversation, RegionSlot::ChatThread, true, true},
    {PageArchetype::ChatConversation, RegionSlot::ChatActionBar, true, false},

    {PageArchetype::ChatCompose, RegionSlot::ChromeTopBar, true, false},
    {PageArchetype::ChatCompose, RegionSlot::ChatComposer, true, false},
    {PageArchetype::ChatCompose, RegionSlot::ChatActionBar, true, false},

    {PageArchetype::DirectoryBrowser, RegionSlot::ChromeTopBar, true, false},
    {PageArchetype::DirectoryBrowser, RegionSlot::DirectoryHeader, true, false},
    {PageArchetype::DirectoryBrowser, RegionSlot::DirectorySelector, true, true},
    {PageArchetype::DirectoryBrowser, RegionSlot::DirectoryContent, true, true},

    {PageArchetype::MapFocus, RegionSlot::ChromeTopBar, true, false},
    {PageArchetype::MapFocus, RegionSlot::MapViewport, true, false},
    {PageArchetype::MapFocus, RegionSlot::MapOverlayPrimary, true, true},
    {PageArchetype::MapFocus, RegionSlot::MapOverlaySecondary, false, true},

    {PageArchetype::InstrumentPanel, RegionSlot::ChromeTopBar, true, false},
    {PageArchetype::InstrumentPanel, RegionSlot::InstrumentCanvas, true, false},
    {PageArchetype::InstrumentPanel, RegionSlot::InstrumentLegend, false, true},

    {PageArchetype::ServicePanel, RegionSlot::ChromeTopBar, true, false},
    {PageArchetype::ServicePanel, RegionSlot::ServicePrimaryPanel, true, false},
    {PageArchetype::ServicePanel, RegionSlot::ServiceFooterActions, false, false},
};

static_assert((sizeof(kPageArchetypes) / sizeof(kPageArchetypes[0])) ==
                  static_cast<std::size_t>(PageArchetype::Count),
              "PageArchetype inventory is out of sync");
static_assert((sizeof(kRegions) / sizeof(kRegions[0])) == static_cast<std::size_t>(RegionSlot::Count),
              "RegionSlot inventory is out of sync");
static_assert((sizeof(kComponents) / sizeof(kComponents[0])) ==
                  static_cast<std::size_t>(ComponentSlot::Count),
              "ComponentSlot inventory is out of sync");
static_assert((sizeof(kBindings) / sizeof(kBindings[0])) == static_cast<std::size_t>(BindingSlot::Count),
              "BindingSlot inventory is out of sync");
static_assert((sizeof(kActions) / sizeof(kActions[0])) == static_cast<std::size_t>(ActionSlot::Count),
              "ActionSlot inventory is out of sync");

template <typename SlotEnum, typename Info, std::size_t N>
const Info* info_from_slot(SlotEnum slot, const Info (&infos)[N])
{
    const std::size_t index = static_cast<std::size_t>(slot);
    return index < N ? &infos[index] : nullptr;
}

template <typename SlotEnum, typename Info, std::size_t N>
const char* id_from_slot(SlotEnum slot, const Info (&infos)[N])
{
    const Info* info = info_from_slot(slot, infos);
    return info ? info->id : "";
}

template <typename SlotEnum, typename Info, std::size_t N>
bool slot_from_id_impl(const char* slot_id, const Info (&infos)[N], SlotEnum& out_slot)
{
    if (!slot_id || !*slot_id)
    {
        return false;
    }

    for (std::size_t i = 0; i < N; ++i)
    {
        if (std::strcmp(slot_id, infos[i].id) == 0)
        {
            out_slot = static_cast<SlotEnum>(i);
            return true;
        }
    }
    return false;
}

template <typename Info, std::size_t N>
std::size_t info_count(const Info (&infos)[N])
{
    return N;
}

} // namespace

const char* page_archetype_id(PageArchetype archetype)
{
    return id_from_slot(archetype, kPageArchetypes);
}

bool page_archetype_from_id(const char* archetype_id, PageArchetype& out_archetype)
{
    return slot_from_id_impl(archetype_id, kPageArchetypes, out_archetype);
}

std::size_t page_archetype_count()
{
    return info_count(kPageArchetypes);
}

bool describe_page_archetype(PageArchetype archetype, PageArchetypeInfo& out_info)
{
    const PageArchetypeInfo* info = info_from_slot(archetype, kPageArchetypes);
    if (!info)
    {
        out_info = PageArchetypeInfo{};
        return false;
    }

    out_info = *info;
    return true;
}

const char* region_slot_id(RegionSlot region)
{
    return id_from_slot(region, kRegions);
}

bool region_slot_from_id(const char* region_id, RegionSlot& out_region)
{
    return slot_from_id_impl(region_id, kRegions, out_region);
}

std::size_t region_slot_count()
{
    return info_count(kRegions);
}

bool describe_region(RegionSlot region, RegionInfo& out_info)
{
    const RegionInfo* info = info_from_slot(region, kRegions);
    if (!info)
    {
        out_info = RegionInfo{};
        return false;
    }

    out_info = *info;
    return true;
}

bool describe_region_support(PageArchetype archetype, RegionSlot region, RegionSupportInfo& out_info)
{
    out_info = RegionSupportInfo{};
    for (const RegionMembership& membership : kRegionMemberships)
    {
        if (membership.archetype == archetype && membership.region == region)
        {
            out_info.supported = true;
            out_info.required = membership.required;
            out_info.repeatable = membership.repeatable;
            return true;
        }
    }
    return false;
}

bool archetype_supports_region(PageArchetype archetype, RegionSlot region)
{
    RegionSupportInfo support;
    return describe_region_support(archetype, region, support) && support.supported;
}

const char* component_slot_id(ComponentSlot component)
{
    return id_from_slot(component, kComponents);
}

bool component_slot_from_id(const char* component_id, ComponentSlot& out_component)
{
    return slot_from_id_impl(component_id, kComponents, out_component);
}

std::size_t component_slot_count()
{
    return info_count(kComponents);
}

bool describe_component(ComponentSlot component, ComponentInfo& out_info)
{
    const ComponentInfo* info = info_from_slot(component, kComponents);
    if (!info)
    {
        out_info = ComponentInfo{};
        return false;
    }

    out_info = *info;
    return true;
}

const char* binding_slot_id(BindingSlot binding)
{
    return id_from_slot(binding, kBindings);
}

bool binding_slot_from_id(const char* binding_id, BindingSlot& out_binding)
{
    return slot_from_id_impl(binding_id, kBindings, out_binding);
}

std::size_t binding_slot_count()
{
    return info_count(kBindings);
}

bool describe_binding(BindingSlot binding, BindingInfo& out_info)
{
    const BindingInfo* info = info_from_slot(binding, kBindings);
    if (!info)
    {
        out_info = BindingInfo{};
        return false;
    }

    out_info = *info;
    return true;
}

const char* action_slot_id(ActionSlot action)
{
    return id_from_slot(action, kActions);
}

bool action_slot_from_id(const char* action_id, ActionSlot& out_action)
{
    return slot_from_id_impl(action_id, kActions, out_action);
}

std::size_t action_slot_count()
{
    return info_count(kActions);
}

bool describe_action(ActionSlot action, ActionInfo& out_info)
{
    const ActionInfo* info = info_from_slot(action, kActions);
    if (!info)
    {
        out_info = ActionInfo{};
        return false;
    }

    out_info = *info;
    return true;
}

} // namespace ui::presentation
