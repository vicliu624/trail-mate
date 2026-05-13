#pragma once

#include "ui_presentation/chat/chat_identity.h"
#include "ui_presentation/chat/chat_workspace_snapshot.h"
#include "ui_presentation/common/ui_action_result.h"

namespace ui::chat
{

class IChatActionSink
{
  public:
    virtual ~IChatActionSink() = default;

    virtual ui::UiActionResult selectConversation(ConversationId id) = 0;
    virtual ui::UiActionResult sendMessage(const SendMessageView& message) = 0;
    virtual ui::UiActionResult markRead(ConversationId id) = 0;
};

} // namespace ui::chat
