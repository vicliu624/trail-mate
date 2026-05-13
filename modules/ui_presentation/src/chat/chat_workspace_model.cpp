#include "ui_presentation/chat/chat_workspace_model.h"

#include <stddef.h>

namespace ui::chat
{

namespace
{

size_t textLength(const char* text)
{
    if (!text)
    {
        return 0;
    }

    size_t length = 0;
    while (text[length] != '\0')
    {
        ++length;
    }
    return length;
}

} // namespace

ChatWorkspaceModel::ChatWorkspaceModel(IChatPresentationSource& source,
                                       IChatActionSink& sink)
    : source_(source),
      sink_(sink)
{
}

ChatWorkspaceSnapshot ChatWorkspaceModel::snapshot() const
{
    ChatWorkspaceSnapshot out{};
    if (!source_.buildChatWorkspaceSnapshot(out,
                                            selected_,
                                            conversation_offset_,
                                            message_offset_))
    {
        out.header.valid = false;
    }
    return out;
}

ui::UiActionResult ChatWorkspaceModel::selectConversation(ConversationId id)
{
    if (!id.isValid())
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    const auto result = sink_.selectConversation(id);
    if (result.ok)
    {
        selected_ = id;
        message_offset_ = 0;
    }
    return result;
}

ui::UiActionResult ChatWorkspaceModel::sendMessage(const char* text)
{
    const size_t length = textLength(text);
    if (!selected_.isValid() || length == 0)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    SendMessageView message;
    message.conversation = selected_;
    message.text = text;
    message.text_len = length;
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
