#pragma once

#include "../../chat/domain/chat_types.h"
#include "lvgl.h"

namespace chat::ui::layout
{

struct ConversationWidgets
{
    lv_obj_t* root = nullptr;

    lv_obj_t* msg_list = nullptr;
    lv_obj_t* action_bar = nullptr;
    lv_obj_t* reply_btn = nullptr;
    lv_obj_t* reply_label = nullptr;
};

/**
 * Root(Column): TopBar(widget) + MsgList(grow=1) + ActionBar(fixed height)
 */
ConversationWidgets create_conversation_base(lv_obj_t* parent);

// Create one message row container (full width, ROW)
lv_obj_t* create_message_row(lv_obj_t* msg_list_parent);

// Create one bubble inside row (COLUMN, size content, max width set later)
lv_obj_t* create_bubble(lv_obj_t* row_parent);

// Create bubble text label
lv_obj_t* create_bubble_text(lv_obj_t* bubble_parent);

// Create bubble time label
lv_obj_t* create_bubble_time(lv_obj_t* bubble_parent);

// Create bubble status label
lv_obj_t* create_bubble_status(lv_obj_t* bubble_parent);

// Layout-only helpers to align message row left/right
void align_message_row(lv_obj_t* row, bool is_self);

// Layout-only helper to apply max width for bubble
void set_bubble_max_width(lv_obj_t* bubble, lv_coord_t max_w);

// Accessors for sizing computation
lv_coord_t get_msg_list_content_width(lv_obj_t* msg_list);

} // namespace chat::ui::layout
