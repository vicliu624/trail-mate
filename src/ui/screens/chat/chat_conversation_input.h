#pragma once

#include "lvgl.h"

namespace chat::ui {

class ChatConversationScreen;

namespace conversation::input {

struct Binding {
    lv_obj_t* msg_list = nullptr;
    lv_obj_t* reply_btn = nullptr;
    lv_obj_t* back_btn = nullptr;
    lv_group_t* group = nullptr;
    bool bound = false;
};

void init(ChatConversationScreen* screen, Binding* binding);
void cleanup(Binding* binding);

} // namespace conversation::input
} // namespace chat::ui
