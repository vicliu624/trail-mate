#pragma once

#include "lvgl.h"

namespace chat::ui::conversation::styles {

void init_once();

// containers
void apply_root(lv_obj_t* root);
void apply_msg_list(lv_obj_t* msg_list);
void apply_action_bar(lv_obj_t* action_bar);

// buttons
void apply_reply_btn(lv_obj_t* btn);
void apply_reply_label(lv_obj_t* label);

// message row + bubble
void apply_message_row(lv_obj_t* row);
void apply_bubble(lv_obj_t* bubble, bool is_self);
void apply_bubble_text(lv_obj_t* label);
void apply_bubble_time(lv_obj_t* label);
void apply_bubble_status(lv_obj_t* label);

} // namespace chat::ui::conversation::styles
