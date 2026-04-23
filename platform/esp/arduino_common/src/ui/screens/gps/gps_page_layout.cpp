#include "ui/screens/gps/gps_page_layout.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/presentation/map_focus_layout.h"

namespace gps::ui::layout
{
namespace map_focus_layout = ::ui::presentation::map_focus_layout;

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
 *         [layer_btn]
 *           [layer_label]
 *       [member_panel: top-left column]
 *         [route_btn]
 *           [route_label]
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
 *       |  |- layer_btn
 *       `- member_panel
 *          `- route_btn
 */
void create(lv_obj_t* parent, const Spec& spec, Widgets& w)
{
    w = Widgets{};

    const auto& profile = ::ui::page_profile::current();
    const lv_coord_t header_height = profile.top_bar_height > 0 ? profile.top_bar_height
                                                                : static_cast<lv_coord_t>(::ui::widgets::kTopBarHeight);

    map_focus_layout::HeaderSpec header_spec{};
    header_spec.height = header_height;
    w.root = map_focus_layout::create_root(parent);
    lv_obj_set_flex_flow(w.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    w.header = map_focus_layout::create_header_container(w.root, header_spec);

    w.content = map_focus_layout::create_content(w.root);
    lv_obj_set_flex_align(w.content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    w.map = map_focus_layout::create_viewport_region(w.content);

    w.resolution_label = lv_label_create(w.map);
    lv_obj_align(w.resolution_label, LV_ALIGN_BOTTOM_LEFT, spec.resolution_x, spec.resolution_y);

    w.altitude_label = lv_label_create(w.map);
    lv_obj_align(w.altitude_label, LV_ALIGN_BOTTOM_MID, spec.altitude_x, spec.altitude_y);

    map_focus_layout::OverlayPanelSpec panel_spec{};
    panel_spec.width = spec.panel_width;
    panel_spec.align = LV_ALIGN_TOP_RIGHT;
    panel_spec.align_y = spec.panel_top_offset;
    panel_spec.pad_row = spec.panel_row_gap;
    w.panel = map_focus_layout::create_overlay_panel(
        w.map, map_focus_layout::OverlayPanelRole::Primary, panel_spec);
    lv_obj_set_flex_align(w.panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);

    map_focus_layout::OverlayPanelSpec member_panel_spec{};
    member_panel_spec.width = spec.member_panel_width;
    member_panel_spec.align = LV_ALIGN_TOP_LEFT;
    member_panel_spec.align_x = spec.member_panel_left_offset;
    member_panel_spec.align_y = spec.member_panel_top_offset;
    member_panel_spec.pad_row = spec.panel_row_gap;
    w.member_panel = map_focus_layout::create_overlay_panel(
        w.map, map_focus_layout::OverlayPanelRole::Secondary, member_panel_spec);
    lv_obj_set_flex_align(w.member_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    auto create_control = [&](lv_obj_t*& btn, lv_obj_t*& label, const char* text)
    {
        btn = lv_btn_create(w.panel);
        lv_obj_set_size(btn, spec.control_btn_w, spec.control_btn_h);
        label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_center(label);
    };

    const bool touch_layout = profile.large_touch_hitbox;
    const bool show_pan_axis_controls = !touch_layout;

    create_control(w.zoom_btn, w.zoom_label, touch_layout ? "Zoom" : "[Z]oom");
    create_control(w.pos_btn, w.pos_label, touch_layout ? "Position" : "[P]osition");
    if (show_pan_axis_controls)
    {
        create_control(w.pan_h_btn, w.pan_h_label, "[H]oriz");
        create_control(w.pan_v_btn, w.pan_v_label, "[V]ert");
    }
    create_control(w.tracker_btn, w.tracker_label, touch_layout ? "Track" : "[T]racker");
    create_control(w.layer_btn, w.layer_label, touch_layout ? "Layer" : "[L]ayer");

    w.route_btn = lv_btn_create(w.member_panel);
    lv_obj_set_width(w.route_btn, LV_PCT(100));
    lv_obj_set_height(w.route_btn, spec.control_btn_h);
    w.route_label = lv_label_create(w.route_btn);
    lv_label_set_text(w.route_label, ::ui::i18n::tr(touch_layout ? "Route" : "[R]oute"));
    lv_obj_center(w.route_label);
}

} // namespace gps::ui::layout
