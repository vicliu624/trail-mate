#include "ui/screens/chat/chat_send_flow.h"

#include "chat/usecase/chat_service.h"
#include "ui/screens/chat/chat_compose_components.h"

namespace chat::ui::send_flow
{

bool begin_local_text_send(ChatComposeScreen* compose,
                           chat::ChatService* service,
                           const chat::ConversationId& conv,
                           const std::string& text,
                           void (*done_cb)(bool ok, bool timeout, void*),
                           void* user_data)
{
    if (!compose || !service || text.empty())
    {
        return false;
    }

    const chat::MessageId msg_id = service->sendText(conv.channel, text, conv.peer);
    compose->beginSend(service, msg_id, done_cb, user_data);
    return true;
}

} // namespace chat::ui::send_flow
