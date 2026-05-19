#include "ui/presentation_sources/legacy_chat_action_sink.h"

#include "chat_presentation_adapters/chat_conversation_mapper.h"

#include <string>

namespace ui::presentation_sources
{

LegacyChatActionSink::LegacyChatActionSink(::chat::ChatService& chat_service)
    : chat_service_(chat_service)
{
}

ui::UiActionResult LegacyChatActionSink::selectConversation(
    ui::chat::ConversationId id)
{
    ::chat::ConversationId core_id;
    if (!chat_presentation_adapters::toCoreConversationId(id, core_id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }

    chat_service_.switchChannel(core_id.channel);
    return ui::UiActionResult::success();
}

ui::UiActionResult LegacyChatActionSink::sendMessage(
    const ui::chat::SendMessageView& message)
{
    if (message.text == nullptr || message.text_len == 0)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
    }

    ::chat::ConversationId core_id;
    if (!chat_presentation_adapters::toCoreConversationId(message.conversation,
                                                          core_id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }

    const std::string text(message.text, message.text_len);
    const ::chat::MessageId msg_id =
        chat_service_.sendText(core_id.channel, text, core_id.peer);
    if (msg_id == 0)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }

    const ::chat::ChatMessage* sent = chat_service_.getMessage(msg_id);
    if (sent != nullptr && sent->status == ::chat::MessageStatus::Failed)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Rejected);
    }
    return ui::UiActionResult::success();
}

ui::UiActionResult LegacyChatActionSink::markRead(ui::chat::ConversationId id)
{
    ::chat::ConversationId core_id;
    if (!chat_presentation_adapters::toCoreConversationId(id, core_id))
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
    }

    chat_service_.markConversationRead(core_id);
    return ui::UiActionResult::success();
}

} // namespace ui::presentation_sources
