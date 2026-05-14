#include "ui_presentation/chat/chat_workspace_model.h"

#include <cstring>

namespace ui::chat
{

ChatWorkspaceModel::ChatWorkspaceModel(IChatPresentationSource& source,
                                       IChatActionSink& sink)
    : source_(source),
      sink_(sink)
{
}

bool ChatWorkspaceModel::buildSnapshot(ChatWorkspaceSnapshot& out) const
{
    ChatWorkspaceRequest request;
    request.selected = selected_;
    request.conversation_offset = conversation_offset_;
    request.message_offset = message_offset_;

    if (!source_.buildChatWorkspaceSnapshot(request, out))
    {
        resetChatWorkspaceSnapshot(out);
        out.header.valid = false;
        return false;
    }
    return out.header.valid;
}

ChatWorkspaceSnapshot ChatWorkspaceModel::snapshot() const
{
    ChatWorkspaceSnapshot out{};
    (void)buildSnapshot(out);
    return out;
}

ui::UiActionResult ChatWorkspaceModel::selectConversation(ConversationId id)
{
    if (!id.isValid())
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    selected_ = id;
    message_offset_ = 0;
    return sink_.selectConversation(id);
}

ui::UiActionResult ChatWorkspaceModel::sendMessage(const char* text)
{
    if (!selected_.isValid() || text == nullptr || text[0] == '\0')
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    SendMessageView message;
    message.conversation = selected_;
    message.text = text;
    message.text_len = std::strlen(text);
    return sink_.sendMessage(message);
}

ui::UiActionResult ChatWorkspaceModel::markRead(ConversationId id)
{
    if (!id.isValid())
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }
    return sink_.markRead(id);
}

ConversationId ChatWorkspaceModel::selectedConversation() const
{
    return selected_;
}

void ChatWorkspaceModel::setConversationOffset(uint16_t offset)
{
    conversation_offset_ = offset;
}

void ChatWorkspaceModel::setMessageOffset(uint16_t offset)
{
    message_offset_ = offset;
}

uint16_t ChatWorkspaceModel::conversationOffset() const
{
    return conversation_offset_;
}

uint16_t ChatWorkspaceModel::messageOffset() const
{
    return message_offset_;
}

} // namespace ui::chat
