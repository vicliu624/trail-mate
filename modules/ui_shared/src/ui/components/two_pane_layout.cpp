#include "ui/components/two_pane_layout.h"

namespace ui::components::two_pane_layout
{
namespace
{

void apply_scroll_behavior(lv_obj_t* obj, lv_scrollbar_mode_t mode)
{
    if (!obj) return;
    if (mode == LV_SCROLLBAR_MODE_OFF)
    {
        make_non_scrollable(obj);
        return;
    }
    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, mode);
}

} // namespace

void make_non_scrollable(lv_obj_t* obj)
{
    if (!obj) return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

void apply_base_container_style(lv_obj_t* obj)
{
    if (!obj) return;
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

lv_obj_t* create_root(lv_obj_t* parent, const RootSpec& spec)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_row(root, spec.pad_row, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    apply_base_container_style(root);
    make_non_scrollable(root);
    return root;
}

lv_obj_t* create_header_container(lv_obj_t* parent, const HeaderSpec& spec)
{
    lv_obj_t* header = lv_obj_create(parent);
    lv_obj_set_size(header, LV_PCT(100), spec.height);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(spec.bg_hex), 0);
    lv_obj_set_style_pad_all(header, spec.pad_all, 0);
    apply_base_container_style(header);
    make_non_scrollable(header);
    return header;
}

lv_obj_t* create_content_row(lv_obj_t* parent, const ContentSpec& spec)
{
    lv_obj_t* content = lv_obj_create(parent);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(content, spec.pad_left, 0);
    lv_obj_set_style_pad_right(content, spec.pad_right, 0);
    lv_obj_set_style_pad_top(content, spec.pad_top, 0);
    lv_obj_set_style_pad_bottom(content, spec.pad_bottom, 0);
    apply_base_container_style(content);
    make_non_scrollable(content);
    return content;
}

lv_obj_t* create_side_panel(lv_obj_t* parent, const SidePanelSpec& spec)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_width(panel, spec.width);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, spec.pad_row, LV_PART_MAIN);
    lv_obj_set_style_margin_left(panel, spec.margin_left, LV_PART_MAIN);
    lv_obj_set_style_margin_right(panel, spec.margin_right, LV_PART_MAIN);
    apply_base_container_style(panel);
    apply_scroll_behavior(panel, spec.scrollbar_mode);
    return panel;
}

lv_obj_t* create_main_panel(lv_obj_t* parent, const MainPanelSpec& spec)
{
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_width(panel, 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(panel, spec.pad_all, 0);
    lv_obj_set_style_pad_row(panel, spec.pad_row, 0);
    lv_obj_set_style_pad_left(panel, spec.pad_left, 0);
    lv_obj_set_style_pad_right(panel, spec.pad_right, 0);
    lv_obj_set_style_pad_top(panel, spec.pad_top, 0);
    lv_obj_set_style_pad_bottom(panel, spec.pad_bottom, 0);
    lv_obj_set_style_margin_left(panel, spec.margin_left, LV_PART_MAIN);
    lv_obj_set_style_margin_right(panel, spec.margin_right, LV_PART_MAIN);
    lv_obj_set_style_margin_bottom(panel, spec.margin_bottom, LV_PART_MAIN);
    apply_base_container_style(panel);
    apply_scroll_behavior(panel, spec.scrollbar_mode);
    return panel;
}

} // namespace ui::components::two_pane_layout
