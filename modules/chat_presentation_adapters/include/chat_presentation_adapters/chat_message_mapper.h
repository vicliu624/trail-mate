#pragma once

#include "chat/domain/chat_types.h"
#include "ui_presentation/chat/chat_message_ref.h"

namespace chat_presentation_adapters
{

ui::chat::MessageDeliveryState mapMessageStatus(chat::MessageStatus status);
ui::chat::MessageFailureKind mapMessageFailure(chat::MessageStatus status);
ui::chat::MessageRef toUiMessageRef(const chat::ChatMessage& message);

} // namespace chat_presentation_adapters
