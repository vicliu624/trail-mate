#pragma once

#include "../chat/domain/chat_types.h"
#include "lvgl.h"
#include "screens/chat/chat_compose_components.h"
#include "widgets/ime/ime_widget.h"
#include <memory>

namespace chat
{
class ChatService;
}

namespace chat::ui
{

class ComposeFlow
{
  public:
    struct Callbacks
    {
        void (*on_closed)(bool sent, bool ok, bool timeout, void* user_data) = nullptr;
        void* user_data = nullptr;
    };

    ComposeFlow() = default;
    ~ComposeFlow();

    bool open(lv_obj_t* parent,
              const chat::ConversationId& conv,
              chat::ChatService* service,
              const char* title,
              const char* status,
              lv_group_t* focus_group,
              Callbacks callbacks);

    void close();
    bool isOpen() const;
    ChatComposeScreen* screen() const;

  private:
    static void on_action(ChatComposeScreen::ActionIntent intent, void* user_data);
    static void on_back(void* user_data);
    static void on_send_done(bool ok, bool timeout, void* user_data);

    void handleAction(ChatComposeScreen::ActionIntent intent);
    void handleBack();
    void handleSendDone(bool ok, bool timeout);
    void notifyClosed(bool sent, bool ok, bool timeout);

    lv_obj_t* parent_ = nullptr;
    chat::ConversationId conv_{};
    chat::ChatService* service_ = nullptr;
    std::unique_ptr<ChatComposeScreen> screen_;
    std::unique_ptr<::ui::widgets::ImeWidget> ime_;
    lv_group_t* focus_group_ = nullptr;
    lv_group_t* prev_group_ = nullptr;
    Callbacks callbacks_{};
    bool send_started_ = false;
};

} // namespace chat::ui
