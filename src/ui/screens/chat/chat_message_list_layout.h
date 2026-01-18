#pragma once
#include "lvgl.h"
#include "../../chat/domain/chat_types.h"

namespace chat::ui::layout {

// root/page
lv_obj_t* create_root(lv_obj_t* parent);

// content panel
lv_obj_t* create_panel(lv_obj_t* parent);

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
