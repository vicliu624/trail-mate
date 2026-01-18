/**
 * @file chat_conversation_components.h
 * @brief Chat conversation screen
 */
#pragma once

#include "lvgl.h"
#include "../../chat/domain/chat_types.h"
#include "../../widgets/top_bar.h"
#include <vector>

namespace chat {
namespace ui {

class ChatConversationScreen {
public:
    ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv);
    ~ChatConversationScreen();

    void addMessage(const chat::ChatMessage& msg);
    void clearMessages();
    void scrollToBottom();

    void setActionCallback(void (*cb)(bool compose, void*), void* user_data);

    lv_obj_t* getObj() const { return container_; }

    chat::ChannelId getChannel() const { return conv_.channel; }

    void setHeaderText(const char* title, const char* status = nullptr);
    void updateBatteryFromBoard();
    void setBackCallback(void (*cb)(void*), void* user_data);

private:
    lv_obj_t* container_ = nullptr;
    ::ui::widgets::TopBar top_bar_{};
    lv_obj_t* msg_list_ = nullptr;
    lv_obj_t* action_bar_ = nullptr;
    lv_obj_t* reply_btn_ = nullptr;
    lv_obj_t* compose_btn_ = nullptr; // kept for compatibility (not created in v0)
    chat::ConversationId conv_{};

    void (*action_cb_)(bool compose, void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    void (*back_cb_)(void*) = nullptr;
    void* back_cb_user_data_ = nullptr;

    struct MessageItem {
        chat::ChatMessage msg;
        lv_obj_t* container = nullptr;   // row
        lv_obj_t* text_label = nullptr;  // inside bubble
        lv_obj_t* time_label = nullptr;  // reserved (not used)
        lv_obj_t* status_label = nullptr;// reserved (not used)
    };

    std::vector<MessageItem> messages_;
    static constexpr size_t MAX_DISPLAY_MESSAGES = 100;

    void createMessageItem(const chat::ChatMessage& msg);

    static void action_event_cb(lv_event_t* e);
    static void handle_back(void* user_data);
};

} // namespace ui
} // namespace chat
