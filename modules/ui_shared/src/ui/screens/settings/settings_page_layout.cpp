/**
 * @file settings_page_layout.cpp
 * @brief Settings layout implementation
 */

#include "ui/screens/settings/settings_page_layout.h"
#include "ui/components/two_pane_layout.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/settings/settings_page_styles.h"
#include "ui/screens/settings/settings_state.h"
#include "ui/ui_common.h"

namespace settings::ui::layout
{

lv_obj_t* create_root(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::RootSpec spec;
    spec.pad_row = profile.top_content_gap;
    return ::ui::components::two_pane_layout::create_root(parent, spec);
}

lv_obj_t* create_header(lv_obj_t* root, void (*back_callback)(void*), void* user_data)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::HeaderSpec header_spec;
    header_spec.height = profile.top_bar_height;
    header_spec.bg_hex = ::ui::components::two_pane_styles::kSidePanelBg;
    header_spec.pad_all = 0;
    lv_obj_t* header = ::ui::components::two_pane_layout::create_header_container(root, header_spec);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = profile.top_bar_height;
    ::ui::widgets::top_bar_init(g_state.top_bar, header, cfg);
    ::ui::widgets::top_bar_set_title(g_state.top_bar, ::ui::i18n::tr("Settings"));
    ::ui::widgets::top_bar_set_back_callback(g_state.top_bar, back_callback, user_data);
    ui_update_top_bar_battery(g_state.top_bar);
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
    panel_spec.pad_row = profile.filter_panel_pad_row;
    panel_spec.margin_left = 0;
    panel_spec.margin_right = 0;
    g_state.filter_panel = ::ui::components::two_pane_layout::create_side_panel(parent, panel_spec);
    style::apply_panel_side(g_state.filter_panel);
    lv_obj_set_style_pad_right(g_state.filter_panel, 1, LV_PART_MAIN);
}

void create_list_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::MainPanelSpec panel_spec;
    panel_spec.pad_all = 0;
    panel_spec.pad_row = profile.list_panel_pad_row;
    panel_spec.pad_left = profile.list_panel_pad_left;
    panel_spec.pad_right = profile.list_panel_pad_right;
    panel_spec.margin_left = 0;
    panel_spec.margin_right = 0;
    panel_spec.margin_bottom = profile.list_panel_margin_bottom;
    panel_spec.scrollbar_mode = LV_SCROLLBAR_MODE_AUTO;
    g_state.list_panel = ::ui::components::two_pane_layout::create_main_panel(parent, panel_spec);
    style::apply_panel_main(g_state.list_panel);
}

} // namespace settings::ui::layout
