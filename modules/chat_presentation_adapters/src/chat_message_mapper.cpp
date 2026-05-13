#include "chat_presentation_adapters/chat_message_mapper.h"

namespace chat_presentation_adapters
{

ui::chat::MessageDeliveryState mapMessageStatus(chat::MessageStatus status)
{
    switch (status)
    {
    case chat::MessageStatus::Incoming:
        return ui::chat::MessageDeliveryState::Received;
    case chat::MessageStatus::Queued:
        return ui::chat::MessageDeliveryState::Queued;
    case chat::MessageStatus::Sent:
        return ui::chat::MessageDeliveryState::Sent;
    case chat::MessageStatus::Failed:
        return ui::chat::MessageDeliveryState::Failed;
    }
    return ui::chat::MessageDeliveryState::Unknown;
}

ui::chat::MessageFailureKind mapMessageFailure(chat::MessageStatus status)
{
    return status == chat::MessageStatus::Failed
               ? ui::chat::MessageFailureKind::Unknown
               : ui::chat::MessageFailureKind::None;
}

ui::chat::MessageRef toUiMessageRef(const chat::ChatMessage& message)
{
    ui::chat::MessageRef out;

    if (message.status == chat::MessageStatus::Queued)
    {
        out.origin = ui::chat::MessageOrigin::LocalPending;
    }
    else if (message.status == chat::MessageStatus::Incoming || message.from != 0)
    {
        out.origin = ui::chat::MessageOrigin::RemoteStored;
    }
    else
    {
        out.origin = ui::chat::MessageOrigin::LocalStored;
    }

    out.protocol_id = message.msg_id;
    return out;
}

} // namespace chat_presentation_adapters
