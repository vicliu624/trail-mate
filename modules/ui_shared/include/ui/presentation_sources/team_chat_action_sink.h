#pragma once

#include "ui_presentation/chat/chat_action_sink.h"

#include <stdint.h>

namespace team
{
class TeamController;

namespace ui
{
class ITeamUiStore;
}
} // namespace team

namespace ui::presentation_sources
{

// TeamChatActionSink is the Phase 5.6-f command adapter for team chat.
//
// Pattern:
//   Command Sink / Bounded Presentation Context.
//
// It handles ConversationKind::Team actions only. It must not send Team
// through LegacyChatActionSink or core chat direct/channel send paths.
class TeamChatActionSink final : public ui::chat::IChatActionSink
{
  public:
    TeamChatActionSink(::team::ui::ITeamUiStore& team_store,
                       ::team::TeamController* team_controller,
                       uint8_t team_channel_raw = 2);

    ui::UiActionResult selectConversation(ui::chat::ConversationId id) override;
    ui::UiActionResult sendMessage(const ui::chat::SendMessageView& message) override;
    ui::UiActionResult markRead(ui::chat::ConversationId id) override;

  private:
    ::team::ui::ITeamUiStore& team_store_;
    ::team::TeamController* team_controller_ = nullptr;
    uint8_t team_channel_raw_ = 2;
    uint32_t next_message_id_ = 1;
};

} // namespace ui::presentation_sources
