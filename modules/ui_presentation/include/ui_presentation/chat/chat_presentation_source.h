#pragma once

#include "ui_presentation/chat/chat_identity.h"
#include "ui_presentation/chat/chat_workspace_snapshot.h"

#include <stdint.h>

namespace ui::chat
{

struct ChatWorkspaceRequest
{
    ConversationId selected;
    uint16_t conversation_offset = 0;
    uint16_t message_offset = 0;
};

class IChatPresentationSource
{
  public:
    virtual ~IChatPresentationSource() = default;

    virtual bool buildChatWorkspaceSnapshot(
        const ChatWorkspaceRequest& request,
        ChatWorkspaceSnapshot& out) const = 0;
};

} // namespace ui::chat
