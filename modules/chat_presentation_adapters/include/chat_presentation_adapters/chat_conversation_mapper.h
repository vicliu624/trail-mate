#pragma once

#include "chat/domain/chat_types.h"
#include "ui_presentation/chat/chat_identity.h"

namespace chat_presentation_adapters
{

ui::chat::ChatProtocolKind mapProtocol(chat::MeshProtocol protocol);
ui::chat::ConversationId toUiConversationId(const chat::ConversationId& id);

} // namespace chat_presentation_adapters
