#include "gps_page_layout.h"

namespace gps::ui::layout
{

/**
 * Wireframe (structure only; styles are applied elsewhere)
 *
 * [root: column]
 *   [header: top bar host]
 *   [content: grow]
 *     [map: fills content]
 *       [resolution_label: bottom-left overlay]
 *       [altitude_label: bottom-center overlay]
 *       [panel: top-right column]
 *         [zoom_btn]
 *           [zoom_label]
 *         [pos_btn]
 *           [pos_label]
 *         [pan_h_btn]
 *           [pan_h_label]
 *         [pan_v_btn]
 *           [pan_v_label]
 *         [tracker_btn]
 *           [tracker_label]
 *         [route_btn]
 *           [route_label]
 *       [member_panel: top-left column]
 *
 * Tree View
 * root
 * |- header
 * `- content
 *    `- map
 *       |- resolution_label
 *       |- altitude_label
 *       |- panel
 *       |  |- zoom_btn
 *       |  |- pos_btn
 *       |  |- pan_h_btn
 *       |  |- pan_v_btn
 *       |  |- tracker_btn
 *       |  `- route_btn
 *       `- member_panel
 */
void create(lv_obj_t* parent, const Spec& spec, Widgets& w)
{
    w = Widgets{};

    w.root = lv_obj_create(parent);
    lv_obj_set_size(w.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(w.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(w.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(w.root, LV_SCROLLBAR_MODE_OFF);

    w.header = lv_obj_create(w.root);
    lv_obj_set_size(w.header, LV_PCT(100), ::ui::widgets::kTopBarHeight);
    lv_obj_set_flex_grow(w.header, 0);
    lv_obj_clear_flag(w.header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(w.header, LV_SCROLLBAR_MODE_OFF);

    w.content = lv_obj_create(w.root);
    lv_obj_set_width(w.content, LV_PCT(100));
    lv_obj_set_height(w.content, 0);
    lv_obj_set_flex_grow(w.content, 1);
    lv_obj_set_flex_flow(w.content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w.content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(w.content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(w.content, LV_SCROLLBAR_MODE_OFF);

    w.map = lv_obj_create(w.content);
    lv_obj_set_size(w.map, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(w.map, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(w.map, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(w.map, LV_OBJ_FLAG_CLICKABLE);

    w.resolution_label = lv_label_create(w.map);
    lv_obj_align(w.resolution_label, LV_ALIGN_BOTTOM_LEFT, spec.resolution_x, spec.resolution_y);

    w.altitude_label = lv_label_create(w.map);
    lv_obj_align(w.altitude_label, LV_ALIGN_BOTTOM_MID, spec.altitude_x, spec.altitude_y);

    w.panel = lv_obj_create(w.map);
    lv_obj_set_size(w.panel, spec.panel_width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(w.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w.panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(w.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(w.panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(w.panel, LV_ALIGN_TOP_RIGHT, 0, spec.panel_top_offset);

    w.member_panel = lv_obj_create(w.map);
    lv_obj_set_size(w.member_panel, spec.member_panel_width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(w.member_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w.member_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(w.member_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(w.member_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(w.member_panel, LV_ALIGN_TOP_LEFT, spec.member_panel_left_offset, spec.member_panel_top_offset);

    auto create_control = [&](lv_obj_t*& btn, lv_obj_t*& label, const char* text)
    {
        btn = lv_btn_create(w.panel);
        lv_obj_set_size(btn, spec.control_btn_w, spec.control_btn_h);
        label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    };

    create_control(w.zoom_btn, w.zoom_label, "[Z]oom");
    create_control(w.pos_btn, w.pos_label, "[P]osition");
    create_control(w.pan_h_btn, w.pan_h_label, "[H]oriz");
    create_control(w.pan_v_btn, w.pan_v_label, "[V]ert");
    create_control(w.tracker_btn, w.tracker_label, "[T]racker");
    create_control(w.route_btn, w.route_label, "[R]oute");
}

} // namespace gps::ui::layout
