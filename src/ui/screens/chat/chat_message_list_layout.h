#pragma once
#include "lvgl.h"
#include "../../chat/domain/chat_types.h"

namespace chat::ui::layout {

struct MessageListLayout {
    lv_obj_t* root;
    lv_obj_t* content;
    lv_obj_t* filter_panel;
    lv_obj_t* list_panel;
    lv_obj_t* direct_btn;
    lv_obj_t* broadcast_btn;
    lv_obj_t* team_btn;
};

// root/page
lv_obj_t* create_root(lv_obj_t* parent);

// split content (filter + list)
MessageListLayout create_layout(lv_obj_t* parent);

// message list item (button with labels)
struct MessageItemWidgets {
    lv_obj_t* btn;
    lv_obj_t* name_label;
    lv_obj_t* preview_label;
    lv_obj_t* time_label;
    lv_obj_t* unread_label;
};

MessageItemWidgets create_message_item(lv_obj_t* parent);

// placeholder
lv_obj_t* create_placeholder(lv_obj_t* parent);

} // namespace chat::ui::layout
