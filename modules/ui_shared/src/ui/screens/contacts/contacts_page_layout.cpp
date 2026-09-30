/**
 * @file contacts_page_layout.cpp
 * @brief Contacts layout
 */

#include "ui/screens/contacts/contacts_page_layout.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/components/air_status_footer.h"
#include "ui/components/info_card.h"
#include "ui/components/two_pane_layout.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/contacts/contacts_filter_profile.h"

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

static lv_obj_t* create_filter_button(lv_obj_t* parent, const char* label)
{
    const auto& profile = ::ui::page_profile::current();
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), profile.filter_button_height);
    ::ui::components::two_pane_layout::make_non_scrollable(btn);
    style::apply_btn_filter(btn);

    lv_obj_t* text = lv_label_create(btn);
    ::ui::i18n::set_label_text(text, label);
    style::apply_label_primary(text);
    lv_obj_center(text);
    return btn;
}

bool is_dense_profile()
{
    return ::ui::page_profile::current().filter_button_height <= 24;
}

lv_coord_t dense_status_width()
{
    return 52;
}

static bool same_text(const std::string& lhs, const char* rhs)
{
    return rhs && lhs == rhs;
}

std::string preferred_node_display_name(const chat::contacts::PeerDirectoryItem& node)
{
    const bool reticulum_node =
        node.protocol == chat::contacts::NodeProtocolType::Reticulum ||
        chat::hasReticulumDestinationIdentity(node.reticulum_identity);
    if (reticulum_node)
    {
        if (node.long_name[0] != '\0' && !same_text(node.long_name, node.short_name))
        {
            return node.long_name;
        }
        if (!node.display_name.empty() && !same_text(node.display_name, node.short_name))
        {
            return node.display_name;
        }
    }

    if (node.is_contact && !node.display_name.empty())
    {
        return node.display_name;
    }

    const bool display_is_short = same_text(node.display_name, node.short_name);
    if (!node.display_name.empty() && !display_is_short)
    {
        return node.display_name;
    }
    if (node.long_name[0] != '\0')
    {
        return node.long_name;
    }
    if (!node.display_name.empty())
    {
        return node.display_name;
    }
    if (node.short_name[0] != '\0')
    {
        return node.short_name;
    }
    return "--";
}

void apply_single_line(lv_obj_t* label)
{
    if (!label)
    {
        return;
    }
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, ::ui::page_profile::resolve_body_font(), 0);
}

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
    ::ui::widgets::top_bar_set_title(g_contacts_state.top_bar, ::ui::i18n::tr("Contacts"));
    ::ui::widgets::top_bar_set_back_callback(g_contacts_state.top_bar, back_callback, user_data);

    return header;
}

void create_footer(lv_obj_t* root)
{
    g_contacts_state.air_status_footer = ::ui::components::air_status_footer::create(root);
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
    const bool reticulum_profile = uses_reticulum_filter_profile();
    ::ui::components::two_pane_layout::SidePanelSpec panel_spec;
    panel_spec.width = profile.filter_panel_width;
    panel_spec.pad_row = profile.filter_panel_pad_row > 0 ? profile.filter_panel_pad_row : kButtonSpacing;
    panel_spec.margin_left = 0;
    panel_spec.margin_right = 0;
    g_contacts_state.filter_panel = ::ui::components::two_pane_layout::create_side_panel(parent, panel_spec);

    style::apply_panel_side(g_contacts_state.filter_panel);

    g_contacts_state.contacts_btn = create_filter_button(g_contacts_state.filter_panel, "Contacts");
    g_contacts_state.nearby_btn = create_filter_button(
        g_contacts_state.filter_panel,
        reticulum_profile ? "Announced" : "Nearby");

    if (reticulum_profile)
    {
        g_contacts_state.groups_btn = create_filter_button(g_contacts_state.filter_panel, "Groups");
        g_contacts_state.ignored_btn = create_filter_button(g_contacts_state.filter_panel, "Ignored");
        g_contacts_state.broadcast_btn = nullptr;
        g_contacts_state.discover_btn = nullptr;

        g_contacts_state.team_btn = create_filter_button(g_contacts_state.filter_panel, "Team");
        lv_obj_add_flag(g_contacts_state.team_btn, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    g_contacts_state.groups_btn = nullptr;
    g_contacts_state.ignored_btn = create_filter_button(g_contacts_state.filter_panel, "Ignored");
    g_contacts_state.broadcast_btn = create_filter_button(g_contacts_state.filter_panel, "Broadcast");
    g_contacts_state.team_btn = create_filter_button(g_contacts_state.filter_panel, "Team");
    lv_obj_add_flag(g_contacts_state.team_btn, LV_OBJ_FLAG_HIDDEN);

    g_contacts_state.discover_btn = create_filter_button(g_contacts_state.filter_panel, "Discover");
    if (!uses_meshcore_filter_profile())
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
        lv_obj_set_style_pad_row(g_contacts_state.sub_container, is_dense_profile() ? 1 : 2, LV_PART_MAIN);
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
                           const chat::contacts::PeerDirectoryItem& node,
                           ContactsMode,
                           const char* status_text)
{
    const auto& profile = ::ui::page_profile::current();
    lv_obj_t* item = lv_obj_create(parent);
    lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
    if (::ui::components::info_card::use_tdeck_layout())
    {
        ::ui::components::info_card::configure_item(item, profile.list_item_height);
    }
    else
    {
        lv_obj_set_size(item, LV_PCT(100), profile.list_item_height);
        ::ui::components::two_pane_layout::make_non_scrollable(item);
    }

    style::apply_list_item(item);

    std::string display_name = preferred_node_display_name(node);

    if (::ui::components::info_card::use_tdeck_layout())
    {
        const auto slots = ::ui::components::info_card::create_content(item);
        ::ui::i18n::set_content_label_text_raw(slots.header_main_label, display_name.c_str());
        style::apply_label_primary(slots.header_main_label);

        ::ui::i18n::set_label_text(slots.body_main_label, status_text);
        style::apply_label_muted(slots.body_main_label);
        lv_obj_set_style_text_align(slots.body_main_label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(slots.body_main_label, ::ui::page_profile::resolve_caption_font(), 0);
    }
    else
    {
        if (is_dense_profile())
        {
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(item,
                                  LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_left(item, 4, LV_PART_MAIN);
            lv_obj_set_style_pad_right(item, 4, LV_PART_MAIN);
            lv_obj_set_style_pad_column(item, 3, LV_PART_MAIN);

            lv_obj_t* name_label = lv_label_create(item);
            ::ui::i18n::set_content_label_text_raw(name_label, display_name.c_str());
            lv_obj_set_width(name_label, 0);
            lv_obj_set_flex_grow(name_label, 1);
            style::apply_label_primary(name_label);
            apply_single_line(name_label);

            lv_obj_t* status_label = lv_label_create(item);
            ::ui::i18n::set_label_text(status_label, status_text);
            lv_obj_set_width(status_label, dense_status_width());
            lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
            style::apply_label_muted(status_label);
            lv_obj_set_style_text_font(status_label, ::ui::page_profile::resolve_caption_font(), 0);
        }
        else
        {
            lv_obj_set_height(item, LV_SIZE_CONTENT);
            lv_obj_set_style_min_height(item, 46, 0);
            lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
            lv_obj_set_style_pad_left(item, 8, 0);
            lv_obj_set_style_pad_right(item, 8, 0);
            lv_obj_set_style_pad_top(item, 4, 0);
            lv_obj_set_style_pad_bottom(item, 4, 0);
            lv_obj_set_style_pad_row(item, 2, 0);
            lv_obj_t* name_label = lv_label_create(item);
            ::ui::i18n::set_content_label_text_raw(name_label, display_name.c_str());
            style::apply_label_primary(name_label);
            lv_obj_set_width(name_label, LV_PCT(100));
            apply_single_line(name_label);

            lv_obj_t* status_label = lv_label_create(item);
            ::ui::i18n::set_label_text(status_label, status_text);
            style::apply_label_muted(status_label);
            lv_obj_set_width(status_label, LV_PCT(100));
            lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
            apply_single_line(status_label);
            lv_obj_set_style_text_font(status_label, ::ui::page_profile::resolve_caption_font(), 0);
        }
    }

    g_contacts_state.list_items.push_back(item);
    return item;
}

} // namespace layout
} // namespace ui
} // namespace contacts
