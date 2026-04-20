#pragma once

#include "chat/domain/chat_types.h"
#include <string>

namespace chat
{
class ChatService;
}

namespace chat::ui
{
class ChatComposeScreen;
}

namespace chat::ui::send_flow
{

bool begin_local_text_send(ChatComposeScreen* compose,
                           chat::ChatService* service,
                           const chat::ConversationId& conv,
                           const std::string& text,
                           void (*done_cb)(bool ok, bool timeout, void*),
                           void* user_data);

} // namespace chat::ui::send_flow
