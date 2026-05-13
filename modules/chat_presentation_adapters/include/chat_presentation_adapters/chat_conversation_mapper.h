#pragma once

#include "chat/domain/chat_types.h"
#include "ui_presentation/chat/chat_identity.h"

namespace chat_presentation_adapters
{

ui::chat::ChatProtocolKind mapProtocol(chat::MeshProtocol protocol);
bool tryMapProtocol(ui::chat::ChatProtocolKind protocol,
                    chat::MeshProtocol& out);
ui::chat::ConversationId toUiConversationId(const chat::ConversationId& id);
bool toCoreConversationId(const ui::chat::ConversationId& id,
                          chat::ConversationId& out);

} // namespace chat_presentation_adapters
