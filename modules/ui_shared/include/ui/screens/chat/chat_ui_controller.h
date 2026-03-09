/**
 * @file chat_ui_controller.h
 * @brief Public shared Chat UI controller header
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/usecase/chat_service.h"
#include "lvgl.h"
#include "sys/event_bus.h"
#include "ui/chat_ui_runtime.h"
#include "ui/screens/chat/chat_compose_components.h"
#include "ui/screens/chat/chat_conversation_components.h"
#include "ui/screens/chat/chat_message_list_components.h"
#include "ui/widgets/ime/ime_widget.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace chat
{
namespace ui
{

class UiController : public IChatUiRuntime
{
  public:
    using State = ChatUiState;
    using ExitRequestCallback = void (*)(void*);

    UiController(lv_obj_t* parent,
                 chat::ChatService& service,
                 chat::ChannelId initial_channel = chat::ChannelId::PRIMARY,
                 ExitRequestCallback exit_request = nullptr,
                 void* exit_request_user_data = nullptr);
    ~UiController();

    void init();
    void update() override;
    void onInput(const sys::InputEvent& event);
    void onChatEvent(sys::Event* event) override;
    void backToList();
    void handleConversationAction(ChatConversationScreen::ActionIntent intent);
    void handleComposeAction(ChatComposeScreen::ActionIntent intent);
    void exitToMenu();

    State getState() const override { return state_; }
    bool isTeamConversationActive() const override { return team_conv_active_; }
    lv_obj_t* getParent() const { return parent_; }
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
    ExitRequestCallback exit_request_ = nullptr;
    void* exit_request_user_data_ = nullptr;

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
    bool sendTeamLocationWithIcon(uint8_t icon_id);
    void openTeamPositionPicker();
    void closeTeamPositionPicker(bool restore_group);
    void updateTeamPositionPickerHint(uint8_t icon_id);
    void onTeamPositionIconSelected(uint8_t icon_id);
    void onTeamPositionCancel();
    bool isTeamPositionPickerOpen() const;

    struct TeamPositionIconEventCtx
    {
        UiController* controller = nullptr;
        uint8_t icon_id = 0;
    };

    std::vector<TeamPositionIconEventCtx*> team_position_icon_ctxs_;
    lv_obj_t* team_position_picker_overlay_ = nullptr;
    lv_obj_t* team_position_picker_panel_ = nullptr;
    lv_obj_t* team_position_picker_desc_ = nullptr;
    lv_group_t* team_position_picker_group_ = nullptr;
    lv_group_t* team_position_prev_group_ = nullptr;

    static void team_position_icon_event_cb(lv_event_t* e);
    static void team_position_cancel_event_cb(lv_event_t* e);
};

} // namespace ui
} // namespace chat

