/**
 * @file chat_conversation_components.h
 * @brief Chat conversation screen
 */
#pragma once

#include "../../chat/domain/chat_types.h"
#include "../../widgets/top_bar.h"
#include "chat_conversation_input.h"
#include "lvgl.h"
#include <vector>

namespace chat
{
namespace ui
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
    lv_obj_t* getBackBtn() const { return top_bar_.back_btn; }

    chat::ChannelId getChannel() const { return conv_.channel; }

    void setHeaderText(const char* title, const char* status = nullptr);
    void updateBatteryFromBoard();
    void setBackCallback(void (*cb)(void*), void* user_data);

  private:
    enum class TimerDomain
    {
        ScreenGeneral,
        Input
    };

    struct TimerEntry
    {
        lv_timer_t* timer = nullptr;
        TimerDomain domain = TimerDomain::ScreenGeneral;
    };

    struct LifetimeGuard
    {
        bool alive = false;
        int pending_async = 0;
    };

    struct ActionContext
    {
        ChatConversationScreen* screen = nullptr;
        ActionIntent intent = ActionIntent::Reply;
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

    lv_obj_t* container_ = nullptr;
    ::ui::widgets::TopBar top_bar_{};
    lv_obj_t* msg_list_ = nullptr;
    lv_obj_t* action_bar_ = nullptr;
    lv_obj_t* reply_btn_ = nullptr;
    lv_obj_t* compose_btn_ = nullptr; // kept for compatibility (not created in v0)
    chat::ConversationId conv_{};

    void (*action_cb_)(ActionIntent intent, void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    void (*back_cb_)(void*) = nullptr;
    void* back_cb_user_data_ = nullptr;

    struct MessageItem
    {
        chat::ChatMessage msg;
        lv_obj_t* container = nullptr;    // row
        lv_obj_t* text_label = nullptr;   // inside bubble
        lv_obj_t* time_label = nullptr;   // reserved (not used)
        lv_obj_t* status_label = nullptr; // reserved (not used)
    };

    std::vector<MessageItem> messages_;
    static constexpr size_t MAX_DISPLAY_MESSAGES = 100;

    LifetimeGuard* guard_ = nullptr;
    std::vector<TimerEntry> timers_;
    conversation::input::Binding input_binding_{};
    ActionContext reply_ctx_{};

    void createMessageItem(const chat::ChatMessage& msg);

    static void action_event_cb(lv_event_t* e);
    static void async_action_cb(void* user_data);
    static void async_back_cb(void* user_data);
    static void on_root_deleted(lv_event_t* e);
    static void handle_back(void* user_data);

    lv_timer_t* add_timer(lv_timer_cb_t cb, uint32_t period_ms, void* user_data, TimerDomain domain);
    void clear_timers(TimerDomain domain);
    void clear_all_timers();
    void handle_root_deleted();

    void schedule_action_async(ActionIntent intent);
    void schedule_back_async();
};

} // namespace ui
} // namespace chat
