#pragma once

#include "chat/usecase/chat_service.h"
#include "ui_presentation/chat/chat_action_sink.h"

namespace ui::presentation_sources
{

class LegacyChatActionSink final : public ui::chat::IChatActionSink
{
  public:
    explicit LegacyChatActionSink(::chat::ChatService& chat_service);

    ui::UiActionResult selectConversation(ui::chat::ConversationId id) override;
    ui::UiActionResult sendMessage(const ui::chat::SendMessageView& message) override;
    ui::UiActionResult markRead(ui::chat::ConversationId id) override;

  private:
    ::chat::ChatService& chat_service_;
};

} // namespace ui::presentation_sources
