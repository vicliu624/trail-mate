/**
 * @file settings_page_layout.cpp
 * @brief Settings layout implementation
 */

#include "settings_page_layout.h"
#include "../../ui_common.h"
#include "settings_page_styles.h"
#include "settings_state.h"

namespace settings::ui::layout
{

static constexpr int kFilterPanelWidth = 120;
static constexpr int kTopBarContentGap = 3;

static void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void apply_base_container_style(lv_obj_t* obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    make_non_scrollable(obj);
}

lv_obj_t* create_root(lv_obj_t* parent)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    apply_base_container_style(root);
    lv_obj_set_style_pad_row(root, kTopBarContentGap, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    return root;
}

lv_obj_t* create_header(lv_obj_t* root, void (*back_callback)(void*), void* user_data)
{
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_size(header, LV_PCT(100), ::ui::widgets::kTopBarHeight);
    lv_obj_set_style_bg_color(header, lv_color_white(), 0);
    apply_base_container_style(header);
    lv_obj_set_style_pad_all(header, 0, 0);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(g_state.top_bar, header, cfg);
    ::ui::widgets::top_bar_set_title(g_state.top_bar, "Settings");
    ::ui::widgets::top_bar_set_back_callback(g_state.top_bar, back_callback, user_data);
    ui_update_top_bar_battery(g_state.top_bar);
    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    apply_base_container_style(content);
    lv_obj_set_style_pad_left(content, 0, 0);
    lv_obj_set_style_pad_right(content, 0, 0);
    lv_obj_set_style_pad_top(content, 0, 0);
    lv_obj_set_style_pad_bottom(content, 0, 0);

    return content;
}

void create_filter_panel(lv_obj_t* parent)
{
    g_state.filter_panel = lv_obj_create(parent);
    make_non_scrollable(g_state.filter_panel);
    style::apply_panel_side(g_state.filter_panel);
    lv_obj_set_width(g_state.filter_panel, kFilterPanelWidth);
    lv_obj_set_height(g_state.filter_panel, LV_PCT(100));
    lv_obj_set_flex_flow(g_state.filter_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_state.filter_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_margin_left(g_state.filter_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(g_state.filter_panel, 0, LV_PART_MAIN);
}

void create_list_panel(lv_obj_t* parent)
{
    g_state.list_panel = lv_obj_create(parent);
    lv_obj_set_scrollbar_mode(g_state.list_panel, LV_SCROLLBAR_MODE_AUTO);
    style::apply_panel_main(g_state.list_panel);

    lv_obj_set_height(g_state.list_panel, LV_PCT(100));
    lv_obj_set_width(g_state.list_panel, 0);
    lv_obj_set_flex_grow(g_state.list_panel, 1);

    lv_obj_set_flex_flow(g_state.list_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_state.list_panel, 6, LV_PART_MAIN);
    lv_obj_set_style_margin_left(g_state.list_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(g_state.list_panel, 3, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(g_state.list_panel, 3, LV_PART_MAIN);
}

} // namespace settings::ui::layout
