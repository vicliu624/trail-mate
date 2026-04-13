/**
 * @file contacts_page_layout.cpp
 * @brief Contacts layout
 */

#include "ui/screens/contacts/contacts_page_layout.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "ui/components/two_pane_layout.h"
#include "ui/page/page_profile.h"

#include <cstdio>

using namespace contacts::ui;

namespace contacts
{
namespace ui
{
namespace layout
{

// Layout constants
static constexpr int kButtonSpacing = 3;
static constexpr int kPanelGap = 3; // Gap between filter and list columns

namespace
{

void format_contacts_title(char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    app::IAppFacade& app_ctx = app::appFacade();
    const chat::MeshConfig& config = app_ctx.getConfig().meshtastic_config;
    chat::MeshProtocol protocol = app_ctx.getConfig().mesh_protocol;
    if (protocol == chat::MeshProtocol::Meshtastic)
    {
        float freq_mhz =
            chat::meshtastic::estimateFrequencyMhz(config.region, config.modem_preset);
        snprintf(out, out_len, "Contacts (Meshtastic - %.3fMHz)", freq_mhz);
        return;
    }
    if (protocol == chat::MeshProtocol::MeshCore)
    {
        snprintf(out, out_len, "Contacts (MeshCore)");
        return;
    }
    snprintf(out, out_len, "Contacts");
}

} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::RootSpec spec;
    spec.pad_row = profile.top_content_gap;
    return ::ui::components::two_pane_layout::create_root(parent, spec);
}

lv_obj_t* create_header(lv_obj_t* root,
                        void (*back_callback)(void*),
                        void* user_data)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::HeaderSpec header_spec;
    header_spec.height = profile.top_bar_height;
    header_spec.bg_hex = ::ui::components::two_pane_styles::kSidePanelBg;
    header_spec.pad_all = 0;
    lv_obj_t* header = ::ui::components::two_pane_layout::create_header_container(root, header_spec);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = profile.top_bar_height;
    ::ui::widgets::top_bar_init(g_contacts_state.top_bar, header, cfg);
    char title[64];
    format_contacts_title(title, sizeof(title));
    ::ui::widgets::top_bar_set_title(g_contacts_state.top_bar, title);
    ::ui::widgets::top_bar_set_back_callback(g_contacts_state.top_bar, back_callback, user_data);

    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::ContentSpec spec;
    spec.pad_left = profile.content_pad_left;
    spec.pad_right = profile.content_pad_right;
    spec.pad_top = profile.content_pad_top;
    spec.pad_bottom = profile.content_pad_bottom;
    return ::ui::components::two_pane_layout::create_content_row(root, spec);
}

void create_filter_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::SidePanelSpec panel_spec;
    panel_spec.width = profile.filter_panel_width;
    panel_spec.pad_row = profile.filter_panel_pad_row > 0 ? profile.filter_panel_pad_row : kButtonSpacing;
    panel_spec.margin_left = 0;
    panel_spec.margin_right = kPanelGap;
    g_contacts_state.filter_panel = ::ui::components::two_pane_layout::create_side_panel(parent, panel_spec);

    style::apply_panel_side(g_contacts_state.filter_panel);

    g_contacts_state.contacts_btn = lv_btn_create(g_contacts_state.filter_panel);
    lv_obj_set_size(g_contacts_state.contacts_btn, LV_PCT(100), profile.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(g_contacts_state.contacts_btn);
    style::apply_btn_filter(g_contacts_state.contacts_btn);
    lv_obj_t* contacts_label = lv_label_create(g_contacts_state.contacts_btn);
    lv_label_set_text(contacts_label, "Contacts");
    style::apply_label_primary(contacts_label);
    lv_obj_center(contacts_label);

    g_contacts_state.nearby_btn = lv_btn_create(g_contacts_state.filter_panel);
    lv_obj_set_size(g_contacts_state.nearby_btn, LV_PCT(100), profile.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(g_contacts_state.nearby_btn);
    style::apply_btn_filter(g_contacts_state.nearby_btn);
    lv_obj_t* nearby_label = lv_label_create(g_contacts_state.nearby_btn);
    lv_label_set_text(nearby_label, "Nearby");
    style::apply_label_primary(nearby_label);
    lv_obj_center(nearby_label);

    g_contacts_state.ignored_btn = lv_btn_create(g_contacts_state.filter_panel);
    lv_obj_set_size(g_contacts_state.ignored_btn, LV_PCT(100), profile.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(g_contacts_state.ignored_btn);
    style::apply_btn_filter(g_contacts_state.ignored_btn);
    lv_obj_t* ignored_label = lv_label_create(g_contacts_state.ignored_btn);
    lv_label_set_text(ignored_label, "Ignored");
    style::apply_label_primary(ignored_label);
    lv_obj_center(ignored_label);

    g_contacts_state.broadcast_btn = lv_btn_create(g_contacts_state.filter_panel);
    lv_obj_set_size(g_contacts_state.broadcast_btn, LV_PCT(100), profile.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(g_contacts_state.broadcast_btn);
    style::apply_btn_filter(g_contacts_state.broadcast_btn);
    lv_obj_t* broadcast_label = lv_label_create(g_contacts_state.broadcast_btn);
    lv_label_set_text(broadcast_label, "Broadcast");
    style::apply_label_primary(broadcast_label);
    lv_obj_center(broadcast_label);

    g_contacts_state.team_btn = lv_btn_create(g_contacts_state.filter_panel);
    lv_obj_set_size(g_contacts_state.team_btn, LV_PCT(100), profile.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(g_contacts_state.team_btn);
    style::apply_btn_filter(g_contacts_state.team_btn);
    lv_obj_t* team_label = lv_label_create(g_contacts_state.team_btn);
    lv_label_set_text(team_label, "Team");
    style::apply_label_primary(team_label);
    lv_obj_center(team_label);
    lv_obj_add_flag(g_contacts_state.team_btn, LV_OBJ_FLAG_HIDDEN);

    g_contacts_state.discover_btn = lv_btn_create(g_contacts_state.filter_panel);
    lv_obj_set_size(g_contacts_state.discover_btn, LV_PCT(100), profile.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(g_contacts_state.discover_btn);
    style::apply_btn_filter(g_contacts_state.discover_btn);
    lv_obj_t* discover_label = lv_label_create(g_contacts_state.discover_btn);
    lv_label_set_text(discover_label, "Discover");
    style::apply_label_primary(discover_label);
    lv_obj_center(discover_label);

    if (app::appFacade().getConfig().mesh_protocol != chat::MeshProtocol::MeshCore)
    {
        lv_obj_add_flag(g_contacts_state.discover_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

void create_list_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::MainPanelSpec panel_spec;
    panel_spec.pad_all = 0;
    panel_spec.pad_row = profile.list_panel_pad_row;
    panel_spec.margin_left = 0;
    panel_spec.margin_right = 0;
    panel_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    g_contacts_state.list_panel = ::ui::components::two_pane_layout::create_main_panel(parent, panel_spec);

    style::apply_panel_main(g_contacts_state.list_panel);
}

void ensure_list_subcontainers()
{
    if (g_contacts_state.list_panel == nullptr) return;

    if (g_contacts_state.sub_container == nullptr)
    {
        g_contacts_state.sub_container = lv_obj_create(g_contacts_state.list_panel);
        ::ui::components::two_pane_layout::make_non_scrollable(g_contacts_state.sub_container);

        style::apply_container_white(g_contacts_state.sub_container);

        // Let sub_container consume remaining height so bottom_container can sit below it.
        lv_obj_set_width(g_contacts_state.sub_container, LV_PCT(100));
        lv_obj_set_height(g_contacts_state.sub_container, 0);
        lv_obj_set_flex_grow(g_contacts_state.sub_container, 1);

        lv_obj_set_flex_flow(g_contacts_state.sub_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(g_contacts_state.sub_container, 2, LV_PART_MAIN);
    }

    if (g_contacts_state.bottom_container == nullptr)
    {
        g_contacts_state.bottom_container = lv_obj_create(g_contacts_state.list_panel);
        ::ui::components::two_pane_layout::make_non_scrollable(g_contacts_state.bottom_container);

        style::apply_container_white(g_contacts_state.bottom_container);

        lv_obj_set_size(g_contacts_state.bottom_container, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(g_contacts_state.bottom_container, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(g_contacts_state.bottom_container, 2, LV_PART_MAIN);
        lv_obj_set_flex_align(g_contacts_state.bottom_container,
                              LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    }
}

lv_obj_t* create_list_item(lv_obj_t* parent,
                           const chat::contacts::NodeInfo& node,
                           ContactsMode /*mode*/,
                           const char* status_text)
{
    const auto& profile = ::ui::page_profile::current();
    lv_obj_t* item = lv_obj_create(parent);
    lv_obj_set_size(item, LV_PCT(100), profile.list_item_height);

    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    ::ui::components::two_pane_layout::make_non_scrollable(item);

    style::apply_list_item(item);

    std::string display_name;
    if (node.long_name[0] != '\0')
    {
        display_name = node.long_name;
    }
    else if (!node.display_name.empty())
    {
        display_name = node.display_name;
    }
    else
    {
        display_name = node.short_name;
    }

    lv_obj_t* name_label = lv_label_create(item);
    lv_label_set_text(name_label, display_name.c_str());
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 10, 0);
    style::apply_label_primary(name_label);

    lv_obj_t* status_label = lv_label_create(item);
    lv_label_set_text(status_label, status_text);
    lv_obj_align(status_label, LV_ALIGN_RIGHT_MID, -10, 0);
    style::apply_label_muted(status_label);

    g_contacts_state.list_items.push_back(item);
    return item;
}

} // namespace layout
} // namespace ui
} // namespace contacts
