#pragma once

#include "lvgl.h"
#include "../../chat/domain/chat_types.h"
#include "../../widgets/top_bar.h"
#include <string>

namespace ui {
namespace widgets {
class ImeWidget;
} // namespace widgets
} // namespace ui

namespace chat {
class ChatService;
}

namespace chat::ui {

class ChatComposeScreen {
public:
    ChatComposeScreen(lv_obj_t* parent, chat::ConversationId conv);
    ~ChatComposeScreen();

    void setHeaderText(const char* title, const char* status = nullptr);
    std::string getText() const;
    void clearText();

    void beginSend(chat::ChatService* service,
                   chat::MessageId msg_id,
                   void (*done_cb)(bool ok, bool timeout, void*),
                   void* user_data);

    void setActionCallback(void (*cb)(bool send, void*), void* user_data);
    void setBackCallback(void (*cb)(void*), void* user_data);

    void attachImeWidget(::ui::widgets::ImeWidget* widget);
    lv_obj_t* getTextarea() const;
    lv_obj_t* getContent() const;
    lv_obj_t* getActionBar() const;

    lv_obj_t* getObj() const;

private:
    chat::ConversationId conv_;

    void (*action_cb_)(bool send, void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    void (*back_cb_)(void*) = nullptr;
    void* back_cb_user_data_ = nullptr;

    struct Impl;
    Impl* impl_ = nullptr;

    void init_topbar();
    void refresh_len();
    void finishSend(bool ok, bool timeout, const char* message);
    void setSendingText(const char* text);

    static void on_action_click(lv_event_t* e);
    static void on_text_changed(lv_event_t* e);
    static void on_key(lv_event_t* e);
    static void on_back(void* user_data);
    static void on_send_timer(lv_timer_t* timer);

    ::ui::widgets::ImeWidget* ime_widget_ = nullptr;
};

} // namespace chat::ui
