/**
 * Tracker layout aligned with contacts_page:
 * Root(COL) -> Header + Content(ROW)
 * Content -> Filter Panel | List Panel
 * List Panel -> Status Label + List Container + Bottom Bar
 */

#include "ui/screens/tracker/tracker_page_layout.h"
#include "ui/page/page_profile.h"
#include "ui/widgets/top_bar.h"

namespace tracker
{
namespace ui
{
namespace layout
{

namespace
{
const ::ui::page_profile::PageLayoutProfile& page_profile()
{
    return ::ui::page_profile::current();
}

lv_coord_t panel_gap()
{
    return page_profile().large_touch_hitbox ? 6 : 3;
}

lv_coord_t panel_pad()
{
    return page_profile().large_touch_hitbox ? 4 : 3;
}

inline void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

inline void apply_base_container_style(lv_obj_t* obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    make_non_scrollable(obj);
}
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    const auto& profile = page_profile();
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(root, profile.top_content_gap, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    apply_base_container_style(root);
    return root;
}

lv_obj_t* create_header(lv_obj_t* root)
{
    const auto& profile = page_profile();
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_size(header, LV_PCT(100), profile.top_bar_height);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xF6E6C6), 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    apply_base_container_style(header);
    return header;
}

lv_obj_t* create_content(lv_obj_t* root)
{
    const auto& profile = page_profile();
    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(content, profile.content_pad_left, 0);
    lv_obj_set_style_pad_right(content, profile.content_pad_right, 0);
    lv_obj_set_style_pad_top(content, profile.content_pad_top, 0);
    lv_obj_set_style_pad_bottom(content, profile.content_pad_bottom, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    apply_base_container_style(content);
    return content;
}

lv_obj_t* create_filter_panel(lv_obj_t* content, int width)
{
    const auto& profile = page_profile();
    lv_obj_t* panel = lv_obj_create(content);
    lv_obj_set_width(panel, width);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, profile.filter_panel_pad_row, LV_PART_MAIN);
    lv_obj_set_style_margin_left(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(panel, panel_gap(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF6E6C6), 0);
    apply_base_container_style(panel);
    return panel;
}

lv_obj_t* create_list_panel(lv_obj_t* content)
{
    const auto& profile = page_profile();
    lv_obj_t* panel = lv_obj_create(content);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_width(panel, 0);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(panel, profile.list_panel_pad_left, LV_PART_MAIN);
    lv_obj_set_style_pad_right(panel, profile.list_panel_pad_right, LV_PART_MAIN);
    lv_obj_set_style_pad_top(panel, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(panel, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, profile.list_panel_pad_row, LV_PART_MAIN);
    lv_obj_set_style_margin_left(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_right(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFAF0D8), 0);
    apply_base_container_style(panel);
    return panel;
}

lv_obj_t* create_list_container(lv_obj_t* list_panel)
{
    const auto& profile = page_profile();
    lv_obj_t* container = lv_obj_create(list_panel);
    lv_obj_set_width(container, LV_PCT(100));
    lv_obj_set_height(container, 0);
    lv_obj_set_flex_grow(container, 1);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(container, profile.list_panel_pad_row, LV_PART_MAIN);
    lv_obj_set_style_pad_left(container, profile.list_panel_pad_left, LV_PART_MAIN);
    lv_obj_set_style_pad_right(container, profile.list_panel_pad_right, LV_PART_MAIN);
    lv_obj_set_style_pad_top(container, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(container, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(container, lv_color_hex(0xFAF0D8), 0);
    apply_base_container_style(container);
    return container;
}

lv_obj_t* create_bottom_bar(lv_obj_t* list_panel)
{
    const auto& profile = page_profile();
    lv_obj_t* bar = lv_obj_create(list_panel);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, panel_gap(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(bar, profile.list_panel_pad_left, LV_PART_MAIN);
    lv_obj_set_style_pad_right(bar, profile.list_panel_pad_right, LV_PART_MAIN);
    lv_obj_set_style_pad_top(bar, panel_pad(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(bar, profile.list_panel_margin_bottom, LV_PART_MAIN);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xFAF0D8), 0);
    apply_base_container_style(bar);
    return bar;
}

} // namespace layout
} // namespace ui
} // namespace tracker
