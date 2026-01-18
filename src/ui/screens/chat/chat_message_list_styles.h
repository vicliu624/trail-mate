#pragma once
#include "lvgl.h"

namespace chat::ui::message_list::styles {

void init_once();

// containers
void apply_root_container(lv_obj_t* obj);
void apply_panel(lv_obj_t* obj);

// list item
void apply_item_btn(lv_obj_t* btn);

// labels
void apply_label_name(lv_obj_t* label);
void apply_label_preview(lv_obj_t* label);
void apply_label_time(lv_obj_t* label);
void apply_label_unread(lv_obj_t* label);
void apply_label_placeholder(lv_obj_t* label);

} // namespace chat::ui::message_list::styles
