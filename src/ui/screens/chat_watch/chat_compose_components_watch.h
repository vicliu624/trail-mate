#pragma once

#include "../../chat/domain/chat_types.h"
#include "../../widgets/top_bar.h"
#include "lvgl.h"
#include <string>
#include <vector>

namespace ui
{
namespace widgets
{
class ImeWidget;
} // namespace widgets
} // namespace ui

namespace chat
{
class ChatService;
}

namespace input
{
class MorseEngine;
} // namespace input

namespace chat::ui
{

class ChatComposeScreen
{
  public:
    enum class ActionIntent
    {
        Send,
        Position,
        Cancel
    };

    ChatComposeScreen(lv_obj_t* parent, chat::ConversationId conv);
    ~ChatComposeScreen();

    void setHeaderText(const char* title, const char* status = nullptr);
    void setActionLabels(const char* send_label, const char* cancel_label);
    void setPositionButton(const char* label, bool visible);
    std::string getText() const;
    void clearText();

    void beginSend(chat::ChatService* service,
                   chat::MessageId msg_id,
                   void (*done_cb)(bool ok, bool timeout, void*),
                   void* user_data);

    void setActionCallback(void (*cb)(ActionIntent intent, void*), void* user_data);
    void setBackCallback(void (*cb)(void*), void* user_data);

    void attachImeWidget(::ui::widgets::ImeWidget* widget);
    lv_obj_t* getTextarea() const;
    lv_obj_t* getContent() const;
    lv_obj_t* getActionBar() const;

    lv_obj_t* getObj() const;

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
        ActionIntent intent = ActionIntent::Send;
    };

    lv_obj_t* container_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* mic_btn_ = nullptr;
    lv_obj_t* morse_btn_ = nullptr;
    lv_obj_t* preset_btn_ = nullptr;
    lv_obj_t* back_btn_ = nullptr;
    lv_obj_t* morse_title_label_ = nullptr;
    lv_obj_t* morse_status_label_ = nullptr;
    lv_obj_t* morse_level_bar_ = nullptr;
    lv_obj_t* morse_symbol_label_ = nullptr;
    lv_obj_t* morse_text_label_ = nullptr;
    lv_obj_t* morse_hint_label_ = nullptr;
    lv_obj_t* morse_back_btn_ = nullptr;
    lv_timer_t* morse_timer_ = nullptr;
    input::MorseEngine* morse_ = nullptr;

    chat::ConversationId conv_{};

    void (*action_cb_)(ActionIntent intent, void*) = nullptr;
    void* action_cb_user_data_ = nullptr;

    void (*back_cb_)(void*) = nullptr;
    void* back_cb_user_data_ = nullptr;

    std::vector<std::string> quick_texts_;
    std::string selected_text_;
    enum class ViewMode
    {
        Main,
        Preset,
        Morse
    };
    ViewMode view_mode_ = ViewMode::Main;

    LifetimeGuard* guard_ = nullptr;

    void showMain();
    void showPreset();
    void showMorse();
    void stopMorse();
    void updateMorseUi();
    void schedule_action_async(ActionIntent intent);

    static void main_event_cb(lv_event_t* e);
    static void preset_event_cb(lv_event_t* e);
    static void morse_back_event_cb(lv_event_t* e);
    static void morse_timer_cb(lv_timer_t* timer);
    static void async_action_cb(void* user_data);
    static void handle_back(void* user_data);
};

} // namespace chat::ui
