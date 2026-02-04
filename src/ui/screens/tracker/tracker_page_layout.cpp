/**
 * UI Wireframe / Layout Tree
 * ------------------------------------------------------------
 * Root(COL)
 * |-- Header(TopBar host, fixed height)
 * `-- Body(ROW, grow=1)
 *     |-- ModePanel(fixed width)
 *     `-- MainPanel(COL, grow=1)
 */

#include "tracker_page_layout.h"
#include "../../widgets/top_bar.h"
#include "tracker_state.h"

namespace tracker
{
namespace ui
{
namespace layout
{

namespace
{
inline void make_non_scrollable(lv_obj_t* obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

inline void apply_base_container_style(lv_obj_t* obj)
{
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    make_non_scrollable(obj);
}
} // namespace

lv_obj_t* create_root(lv_obj_t* parent)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_pad_row(root, 4, 0);
    lv_obj_set_style_bg_color(root, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    apply_base_container_style(root);
    return root;
}

lv_obj_t* create_header(lv_obj_t* root)
{
    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, ::ui::widgets::kTopBarHeight);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    apply_base_container_style(header);
    return header;
}

lv_obj_t* create_body(lv_obj_t* root)
{
    lv_obj_t* body = lv_obj_create(root);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_bg_color(body, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    apply_base_container_style(body);
    return body;
}

lv_obj_t* create_mode_panel(lv_obj_t* body, int width)
{
    lv_obj_t* panel = lv_obj_create(body);
    lv_obj_set_width(panel, width);
    lv_obj_set_height(panel, LV_PCT(100));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(panel, 4, 0);
    lv_obj_set_style_pad_row(panel, 6, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xF2F2F2), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    apply_base_container_style(panel);
    return panel;
}

lv_obj_t* create_main_panel(lv_obj_t* body)
{
    lv_obj_t* panel = lv_obj_create(body);
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(panel, 4, 0);
    lv_obj_set_style_pad_row(panel, 2, 0);
    lv_obj_set_style_bg_color(panel, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    apply_base_container_style(panel);
    return panel;
}

lv_obj_t* create_section(lv_obj_t* parent)
{
    lv_obj_t* section = lv_obj_create(parent);
    lv_obj_set_width(section, LV_PCT(100));
    lv_obj_set_flex_grow(section, 1);
    lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(section, 4, 0);
    lv_obj_set_style_pad_row(section, 6, 0);
    lv_obj_set_style_bg_color(section, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(section, LV_OPA_COVER, 0);
    apply_base_container_style(section);
    return section;
}

} // namespace layout
} // namespace ui
} // namespace tracker
