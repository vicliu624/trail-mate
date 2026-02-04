/**
 * @file ui_controller.h
 * @brief Chat UI controller
 */

#pragma once

#include "../chat/domain/chat_types.h"
#include "../chat/usecase/chat_service.h"
#include "../sys/event_bus.h"
#include "lvgl.h"
#include "screens/chat/chat_compose_components.h"
#include "screens/chat/chat_conversation_components.h"
#include "screens/chat/chat_message_list_components.h"
#include "widgets/ime/ime_widget.h"
#include <memory>

namespace chat
{
namespace ui
{

/**
 * @brief UI Controller
 * Manages chat UI state machine and coordinates with ChatService
 */
class UiController
{
  public:
    enum class State
    {
        ChannelList,  // Showing message list
        Conversation, // Showing message thread
        Compose       // Showing compose editor
    };

    UiController(lv_obj_t* parent, chat::ChatService& service, chat::ChannelId initial_channel = chat::ChannelId::PRIMARY);
    ~UiController();

    /**
     * @brief Initialize UI
     */
    void init();

    /**
     * @brief Update UI (call from main loop)
     */
    void update();

    /**
     * @brief Handle input event
     */
    void onInput(const sys::InputEvent& event);

    /**
     * @brief Handle chat event
     */
    void onChatEvent(sys::Event* event);

    /**
     * @brief 从会话返回消息列表
     */
    void backToList();

    /**
     * @brief Handle conversation action (reply/compose)
     */
    void handleConversationAction(ChatConversationScreen::ActionIntent intent);

    /**
     * @brief Handle compose action (send/cancel)
     */
    void handleComposeAction(ChatComposeScreen::ActionIntent intent);

    /**
     * @brief Exit chat UI to main menu
     */
    void exitToMenu();

    /**
     * @brief Get current state
     */
    State getState() const
    {
        return state_;
    }

    /**
     * @brief Get parent object
     */
    lv_obj_t* getParent() const
    {
        return parent_;
    }

    /**
     * @brief Handle conversation selection from UI
     */
    void onChannelClicked(chat::ConversationId conv);

  private:
    lv_obj_t* parent_;
    chat::ChatService& service_;
    State state_;

    std::unique_ptr<ChatMessageListScreen> channel_list_;
    std::unique_ptr<ChatConversationScreen> conversation_;
    std::unique_ptr<ChatComposeScreen> compose_;
    std::unique_ptr<::ui::widgets::ImeWidget> compose_ime_;

    chat::ChannelId current_channel_;
    chat::ConversationId current_conv_;
    bool team_conv_active_ = false;
    lv_timer_t* team_conv_timer_ = nullptr;
    bool exiting_ = false;

    void switchToChannelList();
    void switchToConversation(chat::ConversationId conv);
    void switchToCompose(chat::ConversationId conv);
    void handleChannelSelected(const chat::ConversationId& conv);
    void handleSendMessage(const std::string& text);
    void refreshUnreadCounts();
    void cleanupComposeIme();
    bool isTeamConversation(const chat::ConversationId& conv) const;
    void refreshTeamConversation();
    void startTeamConversationTimer();
    void stopTeamConversationTimer();
};

} // namespace ui
} // namespace chat
