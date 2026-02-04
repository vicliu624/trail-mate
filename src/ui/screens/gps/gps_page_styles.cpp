#include "gps_page_styles.h"

namespace gps::ui::styles
{

namespace
{

bool s_inited = false;

lv_style_t s_root_black;
lv_style_t s_header_white;
lv_style_t s_content_black;
lv_style_t s_map_black;
lv_style_t s_panel_transparent;

lv_style_t s_resolution_label;

lv_style_t s_control_btn_main;
lv_style_t s_control_btn_focused;
lv_style_t s_control_btn_pressed;
lv_style_t s_control_btn_disabled;
lv_style_t s_control_btn_label;

lv_style_t s_loading_box;
lv_style_t s_loading_label;

lv_style_t s_toast_box;
lv_style_t s_toast_label;

lv_style_t s_indicator_label;
lv_style_t s_tracker_list;
lv_style_t s_modal_bg;
lv_style_t s_modal_win;

lv_style_t s_zoom_win;
lv_style_t s_zoom_title_bar;
lv_style_t s_zoom_title_label;
lv_style_t s_zoom_content_area;
lv_style_t s_zoom_value_label;
lv_style_t s_zoom_value_label_focused;

constexpr uint32_t kBlack = 0x000000;
constexpr uint32_t kWhite = 0xFFFFFF;
constexpr uint32_t kPanelBtnBg = 0xF4C77A;
constexpr uint32_t kPanelBtnBorder = 0xEBA341;
constexpr uint32_t kPanelBtnFocused = 0xEBA341;
constexpr uint32_t kPanelBtnPressed = 0xF1B65A;
constexpr uint32_t kPanelBtnText = 0x202020;
constexpr uint32_t kResolutionText = 0x808080;
constexpr uint32_t kIndicatorText = 0x00AAFF;
constexpr uint32_t kToastBg = 0x333333;
constexpr uint32_t kToastBorder = 0x666666;
constexpr uint32_t kModalWinBorder = 0x333333;
constexpr uint32_t kZoomWinBg = 0x222222;
constexpr uint32_t kZoomTitleBarBg = 0x2C2C2C;
constexpr uint32_t kZoomValueText = 0x000000;
constexpr uint32_t kZoomValueFocusedBg = 0xF0F0F0;

} // namespace

void init_once()
{
    if (s_inited)
    {
        return;
    }

    lv_style_init(&s_root_black);
    lv_style_set_bg_color(&s_root_black, lv_color_hex(kBlack));
    lv_style_set_bg_opa(&s_root_black, LV_OPA_COVER);
    lv_style_set_border_width(&s_root_black, 0);
    lv_style_set_pad_all(&s_root_black, 0);
    lv_style_set_radius(&s_root_black, 0);
    lv_style_set_pad_row(&s_root_black, 0);

    lv_style_init(&s_header_white);
    lv_style_set_bg_color(&s_header_white, lv_color_hex(kWhite));
    lv_style_set_bg_opa(&s_header_white, LV_OPA_COVER);
    lv_style_set_border_width(&s_header_white, 0);
    lv_style_set_pad_all(&s_header_white, 0);
    lv_style_set_radius(&s_header_white, 0);

    lv_style_init(&s_content_black);
    lv_style_set_bg_color(&s_content_black, lv_color_hex(kBlack));
    lv_style_set_bg_opa(&s_content_black, LV_OPA_COVER);
    lv_style_set_border_width(&s_content_black, 0);
    lv_style_set_pad_all(&s_content_black, 0);
    lv_style_set_radius(&s_content_black, 0);
    lv_style_set_pad_row(&s_content_black, 0);

    lv_style_init(&s_map_black);
    lv_style_set_bg_color(&s_map_black, lv_color_hex(kBlack));
    lv_style_set_bg_opa(&s_map_black, LV_OPA_COVER);
    lv_style_set_border_width(&s_map_black, 0);
    lv_style_set_radius(&s_map_black, 0);
    lv_style_set_pad_all(&s_map_black, 0);
    lv_style_set_margin_all(&s_map_black, 0);

    lv_style_init(&s_panel_transparent);
    lv_style_set_bg_opa(&s_panel_transparent, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_panel_transparent, 0);
    lv_style_set_pad_all(&s_panel_transparent, 0);
    lv_style_set_margin_all(&s_panel_transparent, 0);

    lv_style_init(&s_resolution_label);
    lv_style_set_bg_opa(&s_resolution_label, LV_OPA_TRANSP);
    lv_style_set_text_color(&s_resolution_label, lv_color_hex(kResolutionText));
    lv_style_set_text_font(&s_resolution_label, &lv_font_montserrat_16);
    lv_style_set_text_opa(&s_resolution_label, LV_OPA_COVER);

    lv_style_init(&s_control_btn_main);
    lv_style_set_bg_color(&s_control_btn_main, lv_color_hex(kPanelBtnBg));
    lv_style_set_bg_opa(&s_control_btn_main, LV_OPA_COVER);
    lv_style_set_border_width(&s_control_btn_main, 1);
    lv_style_set_border_color(&s_control_btn_main, lv_color_hex(kPanelBtnBorder));
    lv_style_set_radius(&s_control_btn_main, 6);

    lv_style_init(&s_control_btn_focused);
    lv_style_set_bg_color(&s_control_btn_focused, lv_color_hex(kPanelBtnFocused));
    lv_style_set_bg_opa(&s_control_btn_focused, LV_OPA_COVER);
    lv_style_set_border_width(&s_control_btn_focused, 1);
    lv_style_set_outline_width(&s_control_btn_focused, 0);
    lv_style_set_outline_pad(&s_control_btn_focused, 0);

    lv_style_init(&s_control_btn_pressed);
    lv_style_set_bg_color(&s_control_btn_pressed, lv_color_hex(kPanelBtnPressed));
    lv_style_set_border_width(&s_control_btn_pressed, 1);

    lv_style_init(&s_control_btn_disabled);
    lv_style_set_bg_opa(&s_control_btn_disabled, LV_OPA_50);

    lv_style_init(&s_control_btn_label);
    lv_style_set_text_color(&s_control_btn_label, lv_color_hex(kPanelBtnText));
    lv_style_set_text_font(&s_control_btn_label, &lv_font_montserrat_16);

    lv_style_init(&s_loading_box);
    lv_style_set_bg_color(&s_loading_box, lv_color_hex(kBlack));
    lv_style_set_bg_opa(&s_loading_box, LV_OPA_90);
    lv_style_set_border_width(&s_loading_box, 2);
    lv_style_set_border_color(&s_loading_box, lv_color_hex(kWhite));
    lv_style_set_pad_all(&s_loading_box, 20);

    lv_style_init(&s_loading_label);
    lv_style_set_text_color(&s_loading_label, lv_color_hex(kWhite));
    lv_style_set_text_font(&s_loading_label, &lv_font_montserrat_16);

    lv_style_init(&s_toast_box);
    lv_style_set_bg_color(&s_toast_box, lv_color_hex(kToastBg));
    lv_style_set_bg_opa(&s_toast_box, LV_OPA_90);
    lv_style_set_border_width(&s_toast_box, 1);
    lv_style_set_border_color(&s_toast_box, lv_color_hex(kToastBorder));
    lv_style_set_radius(&s_toast_box, 8);
    lv_style_set_pad_all(&s_toast_box, 12);

    lv_style_init(&s_toast_label);
    lv_style_set_text_color(&s_toast_label, lv_color_hex(kWhite));
    lv_style_set_text_font(&s_toast_label, &lv_font_montserrat_16);
    lv_style_set_text_align(&s_toast_label, LV_TEXT_ALIGN_CENTER);

    lv_style_init(&s_indicator_label);
    lv_style_set_text_color(&s_indicator_label, lv_color_hex(kIndicatorText));
    lv_style_set_text_font(&s_indicator_label, &lv_font_montserrat_16);
    lv_style_set_text_align(&s_indicator_label, LV_TEXT_ALIGN_CENTER);
    lv_style_set_bg_opa(&s_indicator_label, LV_OPA_TRANSP);
    lv_style_set_pad_all(&s_indicator_label, 8);

    lv_style_init(&s_tracker_list);
    lv_style_set_pad_top(&s_tracker_list, 32);

    lv_style_init(&s_modal_bg);
    lv_style_set_bg_color(&s_modal_bg, lv_color_hex(kBlack));
    lv_style_set_bg_opa(&s_modal_bg, LV_OPA_50);
    lv_style_set_border_width(&s_modal_bg, 0);
    lv_style_set_pad_all(&s_modal_bg, 0);

    lv_style_init(&s_modal_win);
    lv_style_set_bg_color(&s_modal_win, lv_color_hex(kWhite));
    lv_style_set_bg_opa(&s_modal_win, LV_OPA_COVER);
    lv_style_set_border_width(&s_modal_win, 2);
    lv_style_set_border_color(&s_modal_win, lv_color_hex(kModalWinBorder));
    lv_style_set_radius(&s_modal_win, 10);
    lv_style_set_pad_all(&s_modal_win, 10);

    lv_style_init(&s_zoom_win);
    lv_style_set_bg_color(&s_zoom_win, lv_color_hex(kZoomWinBg));
    lv_style_set_bg_opa(&s_zoom_win, LV_OPA_COVER);
    lv_style_set_border_width(&s_zoom_win, 2);
    lv_style_set_border_color(&s_zoom_win, lv_color_hex(kWhite));
    lv_style_set_radius(&s_zoom_win, 10);
    lv_style_set_pad_all(&s_zoom_win, 10);
    lv_style_set_outline_width(&s_zoom_win, 2);
    lv_style_set_outline_color(&s_zoom_win, lv_color_hex(kIndicatorText));

    lv_style_init(&s_zoom_title_bar);
    lv_style_set_bg_color(&s_zoom_title_bar, lv_color_hex(kZoomTitleBarBg));
    lv_style_set_bg_opa(&s_zoom_title_bar, LV_OPA_COVER);
    lv_style_set_border_width(&s_zoom_title_bar, 0);
    lv_style_set_pad_all(&s_zoom_title_bar, 8);
    lv_style_set_radius(&s_zoom_title_bar, 0);

    lv_style_init(&s_zoom_title_label);
    lv_style_set_text_color(&s_zoom_title_label, lv_color_hex(kWhite));
    lv_style_set_text_font(&s_zoom_title_label, &lv_font_montserrat_18);

    lv_style_init(&s_zoom_content_area);
    lv_style_set_bg_opa(&s_zoom_content_area, LV_OPA_TRANSP);
    lv_style_set_border_width(&s_zoom_content_area, 0);
    lv_style_set_pad_all(&s_zoom_content_area, 0);

    lv_style_init(&s_zoom_value_label);
    lv_style_set_text_color(&s_zoom_value_label, lv_color_hex(kZoomValueText));
    lv_style_set_text_font(&s_zoom_value_label, &lv_font_montserrat_48);
    lv_style_set_text_align(&s_zoom_value_label, LV_TEXT_ALIGN_CENTER);
    lv_style_set_bg_opa(&s_zoom_value_label, LV_OPA_TRANSP);

    lv_style_init(&s_zoom_value_label_focused);
    lv_style_set_bg_color(&s_zoom_value_label_focused, lv_color_hex(kZoomValueFocusedBg));
    lv_style_set_bg_opa(&s_zoom_value_label_focused, LV_OPA_COVER);
    lv_style_set_outline_width(&s_zoom_value_label_focused, 3);
    lv_style_set_outline_color(&s_zoom_value_label_focused, lv_color_hex(kIndicatorText));
    lv_style_set_outline_pad(&s_zoom_value_label_focused, 6);
    lv_style_set_radius(&s_zoom_value_label_focused, 8);

    s_inited = true;
}

void apply_control_button(lv_obj_t* btn)
{
    if (!btn) return;
    init_once();
    lv_obj_add_style(btn, &s_control_btn_main, LV_PART_MAIN);
    lv_obj_add_style(btn, &s_control_btn_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_style(btn, &s_control_btn_pressed, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_style(btn, &s_control_btn_disabled, LV_PART_MAIN | LV_STATE_DISABLED);
}

void apply_control_button_label(lv_obj_t* label)
{
    if (!label) return;
    init_once();
    lv_obj_add_style(label, &s_control_btn_label, LV_PART_MAIN);
}

void apply_resolution_label(lv_obj_t* label, const layout::Spec& spec)
{
    if (!label) return;
    init_once();
    lv_obj_add_style(label, &s_resolution_label, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, spec.resolution_pad, 0);
}

void apply_panel(lv_obj_t* panel, const layout::Spec& spec)
{
    if (!panel) return;
    init_once();
    lv_obj_add_style(panel, &s_panel_transparent, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, spec.panel_row_gap, 0);
}

void apply_loading_box(lv_obj_t* box)
{
    if (!box) return;
    init_once();
    lv_obj_add_style(box, &s_loading_box, LV_PART_MAIN);
}

void apply_loading_label(lv_obj_t* label)
{
    if (!label) return;
    init_once();
    lv_obj_add_style(label, &s_loading_label, LV_PART_MAIN);
}

void apply_toast_box(lv_obj_t* box)
{
    if (!box) return;
    init_once();
    lv_obj_add_style(box, &s_toast_box, LV_PART_MAIN);
}

void apply_toast_label(lv_obj_t* label)
{
    if (!label) return;
    init_once();
    lv_obj_add_style(label, &s_toast_label, LV_PART_MAIN);
}

void apply_indicator_label(lv_obj_t* label)
{
    if (!label) return;
    init_once();
    lv_obj_add_style(label, &s_indicator_label, LV_PART_MAIN);
}

void apply_tracker_modal_list(lv_obj_t* list)
{
    if (!list) return;
    init_once();
    lv_obj_add_style(list, &s_tracker_list, LV_PART_MAIN);
}

void apply_modal_bg(lv_obj_t* bg)
{
    if (!bg) return;
    init_once();
    lv_obj_add_style(bg, &s_modal_bg, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
}

void apply_modal_win(lv_obj_t* win)
{
    if (!win) return;
    init_once();
    lv_obj_add_style(win, &s_modal_win, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
}

void apply_zoom_popup_win(lv_obj_t* win)
{
    if (!win) return;
    init_once();
    lv_obj_add_style(win, &s_zoom_win, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
}

void apply_zoom_popup_title_bar(lv_obj_t* bar)
{
    if (!bar) return;
    init_once();
    lv_obj_add_style(bar, &s_zoom_title_bar, LV_PART_MAIN);
}

void apply_zoom_popup_title_label(lv_obj_t* label)
{
    if (!label) return;
    init_once();
    lv_obj_add_style(label, &s_zoom_title_label, LV_PART_MAIN);
}

void apply_zoom_popup_content_area(lv_obj_t* area)
{
    if (!area) return;
    init_once();
    lv_obj_add_style(area, &s_zoom_content_area, LV_PART_MAIN);
}

void apply_zoom_popup_value_label(lv_obj_t* label)
{
    if (!label) return;
    init_once();
    lv_obj_add_style(label, &s_zoom_value_label, LV_PART_MAIN);
    lv_obj_add_style(label, &s_zoom_value_label_focused, LV_PART_MAIN | LV_STATE_FOCUSED);
}

void apply_all(const layout::Widgets& w, const layout::Spec& spec)
{
    init_once();

    if (w.root) lv_obj_add_style(w.root, &s_root_black, LV_PART_MAIN);
    if (w.header) lv_obj_add_style(w.header, &s_header_white, LV_PART_MAIN);
    if (w.content) lv_obj_add_style(w.content, &s_content_black, LV_PART_MAIN);
    if (w.map) lv_obj_add_style(w.map, &s_map_black, LV_PART_MAIN);

    apply_panel(w.panel, spec);
    apply_panel(w.member_panel, spec);
    apply_resolution_label(w.resolution_label, spec);

    apply_control_button(w.zoom_btn);
    apply_control_button(w.pos_btn);
    apply_control_button(w.pan_h_btn);
    apply_control_button(w.pan_v_btn);
    apply_control_button(w.tracker_btn);
    apply_control_button(w.route_btn);

    apply_control_button_label(w.zoom_label);
    apply_control_button_label(w.pos_label);
    apply_control_button_label(w.pan_h_label);
    apply_control_button_label(w.pan_v_label);
    apply_control_button_label(w.tracker_label);
    apply_control_button_label(w.route_label);
}

} // namespace gps::ui::styles
