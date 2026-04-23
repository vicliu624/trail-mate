#pragma once
#include "chat/domain/chat_types.h"
#include "lvgl.h"
#include "ui/components/air_status_footer.h"

namespace chat::ui::layout
{

struct MessageListLayout
{
    lv_obj_t* root;
    lv_obj_t* content;
    lv_obj_t* filter_panel;
    lv_obj_t* selector_controls;
    lv_obj_t* list_panel;
    lv_obj_t* direct_btn;
    lv_obj_t* broadcast_btn;
    lv_obj_t* team_btn;
    ::ui::components::air_status_footer::Footer air_status_footer;
};

// directory-browser content scaffold (selector + content panel)
MessageListLayout create_layout(lv_obj_t* parent);

// message list item (clickable row with labels)
struct MessageItemWidgets
{
    lv_obj_t* btn;
    lv_obj_t* name_label;
    lv_obj_t* preview_label;
    lv_obj_t* time_label;
    lv_obj_t* unread_label;
};

MessageItemWidgets create_message_item(lv_obj_t* parent);
void populate_message_item(const MessageItemWidgets& widgets,
                           const chat::ConversationMeta& conv);

// placeholder
lv_obj_t* create_placeholder(lv_obj_t* parent);

} // namespace chat::ui::layout
