#pragma once

#include "ui_presentation/chat/chat_identity.h"
#include "ui_presentation/chat/chat_workspace_snapshot.h"

#include <stdint.h>

namespace ui::chat
{

class IChatPresentationSource
{
  public:
    virtual ~IChatPresentationSource() = default;

    virtual bool buildChatWorkspaceSnapshot(
        ChatWorkspaceSnapshot& out,
        ConversationId selected,
        uint16_t conversation_offset,
        uint16_t message_offset) const = 0;
};

} // namespace ui::chat
