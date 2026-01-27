#pragma once

#include "lvgl.h"
#include "gps_page_layout.h"

namespace gps::ui::styles {

void init_once();
void apply_all(const layout::Widgets& w, const layout::Spec& spec);

void apply_control_button(lv_obj_t* btn);
void apply_control_button_label(lv_obj_t* label);

void apply_status_overlay(lv_obj_t* label, const layout::Spec& spec);
void apply_resolution_label(lv_obj_t* label, const layout::Spec& spec);
void apply_panel(lv_obj_t* panel, const layout::Spec& spec);

void apply_loading_box(lv_obj_t* box);
void apply_loading_label(lv_obj_t* label);

void apply_toast_box(lv_obj_t* box);
void apply_toast_label(lv_obj_t* label);

void apply_indicator_label(lv_obj_t* label);

void apply_tracker_modal_list(lv_obj_t* list);

void apply_modal_bg(lv_obj_t* bg);
void apply_modal_win(lv_obj_t* win);

void apply_zoom_popup_win(lv_obj_t* win);
void apply_zoom_popup_title_bar(lv_obj_t* bar);
void apply_zoom_popup_title_label(lv_obj_t* label);
void apply_zoom_popup_content_area(lv_obj_t* area);
void apply_zoom_popup_value_label(lv_obj_t* label);

} // namespace gps::ui::styles
