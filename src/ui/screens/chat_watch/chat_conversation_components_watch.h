#pragma once

#include "../../chat/domain/chat_types.h"
#include "lvgl.h"
#include <vector>

namespace chat::ui
{

class ChatConversationScreen
{
  public:
    enum class ActionIntent
    {
        Reply
    };

    ChatConversationScreen(lv_obj_t* parent, chat::ConversationId conv);
    ~ChatConversationScreen();

    void addMessage(const chat::ChatMessage& msg);
    void clearMessages();
    void scrollToBottom();

    void setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data);
    bool isAlive() const { return guard_ && guard_->alive; }

    lv_obj_t* getObj() const { return container_; }
    lv_obj_t* getMsgList() const { return msg_list_; }
    lv_obj_t* getReplyBtn() const { return reply_btn_; }
    lv_obj_t* getBackBtn() const { return back_btn_; }

    chat::ChannelId getChannel() const { return conv_.channel; }

    void setHeaderText(const char* title, const char* status = nullptr);
    void updateBatteryFromBoard();
    void setBackCallback(void (*cb)(void*), void* user_data);
    void setReplyEnabled(bool enabled);
    bool isReplyEnabled() const { return reply_enabled_; }

  private:
    struct LifetimeGuard
    {
        bool alive = false;
    };

    struct ActionPayload
    {
        LifetimeGuard* guard = nullptr;
        void (*action_cb)(ActionIntent intent, void*) = nullptr;
        void* user_data = nullptr;
        ActionIntent intent = ActionIntent::Reply;
    };

    struct BackPayload
    {
        LifetimeGuard* guard = nullptr;
        void (*back_cb)(void*) = nullptr;
        void* user_data = nullptr;
    };

    struct MessageItem
    {
        chat::ChatMessage msg;
        lv_obj_t* container = nullptr;
        lv_obj_t* text_label = nullptr;
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* msg_list_ = nullptr;
    lv_obj_t* action_bar_ = nullptr;
    lv_obj_t* reply_btn_ = nullptr;
    lv_obj_t* back_btn_ = nullptr;
    chat::ConversationId conv_{};

    void (*action_cb_)(ActionIntent intent, void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    void (*back_cb_)(void*) = nullptr;
    void* back_cb_user_data_ = nullptr;

    std::vector<MessageItem> messages_;
    static constexpr size_t MAX_DISPLAY_MESSAGES = 100;

    LifetimeGuard* guard_ = nullptr;
    bool reply_enabled_ = true;

    void schedule_action_async(ActionIntent intent);
    void schedule_back_async();

    static void action_event_cb(lv_event_t* e);
    static void back_event_cb(lv_event_t* e);
    static void async_action_cb(void* user_data);
    static void async_back_cb(void* user_data);
    static void handle_back(void* user_data);
};

} // namespace chat::ui
