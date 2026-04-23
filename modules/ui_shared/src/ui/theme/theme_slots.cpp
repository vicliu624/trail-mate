#include "ui/theme/theme_slots.h"

#include <cstring>

namespace ui::theme
{
namespace
{

constexpr const char* kColorSlotIds[] = {
    "color.bg.page",
    "color.bg.surface.primary",
    "color.bg.surface.secondary",
    "color.bg.chrome.topbar",
    "color.bg.overlay.scrim",
    "color.border.default",
    "color.border.strong",
    "color.separator.default",
    "color.text.primary",
    "color.text.muted",
    "color.text.inverse",
    "color.accent.primary",
    "color.accent.strong",
    "color.state.ok",
    "color.state.info",
    "color.state.warn",
    "color.state.error",
    "color.map.background",
    "color.map.route",
    "color.map.track",
    "color.map.marker.border",
    "color.map.signal_label.bg",
    "color.map.node_marker",
    "color.map.self_marker",
    "color.map.link",
    "color.map.readout.id",
    "color.map.readout.lon",
    "color.map.readout.lat",
    "color.map.readout.distance",
    "color.map.readout.protocol",
    "color.map.readout.rssi",
    "color.map.readout.snr",
    "color.map.readout.seen",
    "color.map.readout.zoom",
    "color.team.member.0",
    "color.team.member.1",
    "color.team.member.2",
    "color.team.member.3",
    "color.gnss.system.gps",
    "color.gnss.system.gln",
    "color.gnss.system.gal",
    "color.gnss.system.bd",
    "color.gnss.snr.good",
    "color.gnss.snr.fair",
    "color.gnss.snr.weak",
    "color.gnss.snr.not_used",
    "color.gnss.snr.in_view",
    "color.sstv.meter.mid",
};

constexpr const char* kAssetSlotIds[] = {
    "asset.boot.logo",
    "asset.notification.alert",
    "asset.status.route",
    "asset.status.tracker",
    "asset.status.gps",
    "asset.status.wifi",
    "asset.status.team",
    "asset.status.message",
    "asset.status.ble",
    "asset.menu.app.chat",
    "asset.menu.app.map",
    "asset.menu.app.sky_plot",
    "asset.menu.app.contacts",
    "asset.menu.app.energy_sweep",
    "asset.menu.app.team",
    "asset.menu.app.tracker",
    "asset.menu.app.pc_link",
    "asset.menu.app.sstv",
    "asset.menu.app.settings",
    "asset.menu.app.extensions",
    "asset.menu.app.usb",
    "asset.menu.app.walkie_talkie",
    "asset.map.self_marker",
    "asset.team_location.area_cleared",
    "asset.team_location.base_camp",
    "asset.team_location.good_find",
    "asset.team_location.rally",
    "asset.team_location.sos",
};

struct ComponentSlotAlias
{
    const char* id;
    ComponentSlot slot;
};

constexpr const char* kComponentSlotIds[] = {
    "component.top_bar.container",
    "component.top_bar.back_button",
    "component.top_bar.title_label",
    "component.top_bar.right_label",
    "component.directory_browser.selector_panel",
    "component.directory_browser.content_panel",
    "component.directory_browser.selector_button",
    "component.directory_browser.list_item",
    "component.info_card.base",
    "component.info_card.header",
    "component.busy_overlay.panel",
    "component.busy_overlay.progress_bar",
    "component.notification.banner",
    "component.modal.scrim",
    "component.modal.window",
    "component.menu.app_button",
    "component.menu.app_label",
    "component.menu.bottom_chip.node",
    "component.menu.bottom_chip.ram",
    "component.menu.bottom_chip.psram",
    "component.menu_dashboard.app_grid",
    "component.menu_dashboard.bottom_chips",
    "component.boot_splash.root",
    "component.boot_splash.log",
    "component.watch_face.root",
    "component.watch_face.primary_region",
    "component.watch_face.node_id",
    "component.watch_face.battery",
    "component.watch_face.clock.hour",
    "component.watch_face.clock.minute",
    "component.watch_face.clock.separator",
    "component.watch_face.date",
    "component.chat_conversation.root",
    "component.chat_conversation.thread_region",
    "component.chat_conversation.action_bar",
    "component.chat.bubble.self",
    "component.chat.bubble.peer",
    "component.chat.bubble.text",
    "component.chat.bubble.time",
    "component.chat.bubble.status",
    "component.chat_compose.root",
    "component.chat_compose.content_region",
    "component.chat_compose.editor",
    "component.chat_compose.action_bar",
    "component.map_focus.root",
    "component.map_focus.overlay.primary",
    "component.map_focus.overlay.secondary",
    "component.map.info_panel",
    "component.map.control_button",
    "component.map.layer_popup_window",
    "component.gps.indicator_label",
    "component.gps.loading_box",
    "component.gps.loading_label",
    "component.gps.toast_box",
    "component.gps.toast_label",
    "component.gps.zoom_window",
    "component.gps.zoom_title_bar",
    "component.gps.zoom_title_label",
    "component.gps.zoom_value_label",
    "component.gps.zoom_roller",
    "component.instrument_panel.root",
    "component.instrument_panel.body",
    "component.instrument_panel.canvas_region",
    "component.instrument_panel.legend_region",
    "component.service_panel.root",
    "component.service_panel.body",
    "component.service_panel.primary_panel",
    "component.service_panel.footer_actions",
    "component.team.member_chip",
    "component.gnss.sky_panel",
    "component.gnss.status_panel",
    "component.gnss.status_header",
    "component.gnss.table_header",
    "component.gnss.table_row",
    "component.gnss.status_toggle",
    "component.gnss.satellite_use_tag",
    "component.sstv.image_panel",
    "component.sstv.progress_bar",
    "component.sstv.meter_box",
    "component.action_button.primary",
    "component.action_button.secondary",
};

constexpr ComponentSlotAlias kComponentSlotAliases[] = {
    {"component.two_pane.side_panel", ComponentSlot::DirectoryBrowserSelectorPanel},
    {"component.two_pane.main_panel", ComponentSlot::DirectoryBrowserContentPanel},
    {"component.two_pane.filter_button", ComponentSlot::DirectoryBrowserSelectorButton},
    {"component.two_pane.list_item", ComponentSlot::DirectoryBrowserListItem},
    {"component.chat_conversation.bubble.self", ComponentSlot::ChatBubbleSelf},
    {"component.chat_conversation.bubble.peer", ComponentSlot::ChatBubblePeer},
    {"component.chat_conversation.bubble.text", ComponentSlot::ChatBubbleText},
    {"component.chat_conversation.bubble.time", ComponentSlot::ChatBubbleTime},
    {"component.chat_conversation.bubble.status", ComponentSlot::ChatBubbleStatus},
};

static_assert((sizeof(kColorSlotIds) / sizeof(kColorSlotIds[0])) ==
                  static_cast<std::size_t>(ColorSlot::Count),
              "ColorSlot inventory is out of sync with kColorSlotIds");
static_assert((sizeof(kAssetSlotIds) / sizeof(kAssetSlotIds[0])) ==
                  static_cast<std::size_t>(AssetSlot::Count),
              "AssetSlot inventory is out of sync with kAssetSlotIds");
static_assert((sizeof(kComponentSlotIds) / sizeof(kComponentSlotIds[0])) ==
                  static_cast<std::size_t>(ComponentSlot::Count),
              "ComponentSlot inventory is out of sync with kComponentSlotIds");

template <typename SlotEnum, std::size_t N>
const char* slot_id_from_index(SlotEnum slot, const char* const (&ids)[N])
{
    const std::size_t index = static_cast<std::size_t>(slot);
    return index < N ? ids[index] : "";
}

template <typename SlotEnum, std::size_t N>
bool slot_from_id_impl(const char* slot_id, const char* const (&ids)[N], SlotEnum& out_slot)
{
    if (!slot_id || !*slot_id)
    {
        return false;
    }

    for (std::size_t i = 0; i < N; ++i)
    {
        if (std::strcmp(slot_id, ids[i]) == 0)
        {
            out_slot = static_cast<SlotEnum>(i);
            return true;
        }
    }
    return false;
}

} // namespace

const char* color_slot_id(ColorSlot slot)
{
    return slot_id_from_index(slot, kColorSlotIds);
}

bool color_slot_from_id(const char* slot_id, ColorSlot& out_slot)
{
    return slot_from_id_impl(slot_id, kColorSlotIds, out_slot);
}

std::size_t color_slot_count()
{
    return sizeof(kColorSlotIds) / sizeof(kColorSlotIds[0]);
}

const char* asset_slot_id(AssetSlot slot)
{
    return slot_id_from_index(slot, kAssetSlotIds);
}

bool asset_slot_from_id(const char* slot_id, AssetSlot& out_slot)
{
    return slot_from_id_impl(slot_id, kAssetSlotIds, out_slot);
}

std::size_t asset_slot_count()
{
    return sizeof(kAssetSlotIds) / sizeof(kAssetSlotIds[0]);
}

const char* component_slot_id(ComponentSlot slot)
{
    return slot_id_from_index(slot, kComponentSlotIds);
}

bool component_slot_from_id(const char* slot_id, ComponentSlot& out_slot)
{
    if (slot_from_id_impl(slot_id, kComponentSlotIds, out_slot))
    {
        return true;
    }

    if (!slot_id || !*slot_id)
    {
        return false;
    }

    for (const ComponentSlotAlias& alias : kComponentSlotAliases)
    {
        if (std::strcmp(slot_id, alias.id) == 0)
        {
            out_slot = alias.slot;
            return true;
        }
    }

    return false;
}

std::size_t component_slot_count()
{
    return sizeof(kComponentSlotIds) / sizeof(kComponentSlotIds[0]);
}

} // namespace ui::theme
