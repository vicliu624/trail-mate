#pragma once

#include "ui_presentation/chat/chat_action_sink.h"
#include "ui_presentation/chat/chat_presentation_source.h"

#include <stdint.h>

namespace ui::chat
{

class ChatWorkspaceModel
{
  public:
    ChatWorkspaceModel(IChatPresentationSource& source, IChatActionSink& sink);

    bool buildSnapshot(ChatWorkspaceSnapshot& out) const;
    ChatWorkspaceSnapshot snapshot() const;

    ui::UiActionResult selectConversation(ConversationId id);
    ui::UiActionResult sendMessage(const char* text);
    ui::UiActionResult markRead(ConversationId id);

    ConversationId selectedConversation() const;

    void setConversationOffset(uint16_t offset);
    void setMessageOffset(uint16_t offset);

    uint16_t conversationOffset() const;
    uint16_t messageOffset() const;

  private:
    IChatPresentationSource& source_;
    IChatActionSink& sink_;

    ConversationId selected_{};
    uint16_t conversation_offset_ = 0;
    uint16_t message_offset_ = 0;
};

} // namespace ui::chat
