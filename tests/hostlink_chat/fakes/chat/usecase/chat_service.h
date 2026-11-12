#pragma once
#include "chat/domain/chat_types.h"
#include <functional>
namespace fake_app
{
struct ChatService
{
    bool ready = true;
    unsigned sends = 0;
    std::function<void()> before_send;
    chat::ChatMessage message;
    bool isDataReady() const { return ready; }
    chat::MeshSendResult sendTextToConversationDetailed(const chat::ConversationId& target, const std::string& text)
    {
        if (before_send) before_send();
        ++sends;
        message.protocol = target.protocol;
        message.channel = target.channel;
        message.peer = target.peer;
        message.reticulum_identity = target.reticulum_identity;
        message.msg_id = 123;
        message.text = text;
        message.status = chat::MessageStatus::Queued;
        return chat::MeshSendResult::success(123);
    }
    const chat::ChatMessage* getMessageForProtocol(chat::MessageId id, chat::MeshProtocol protocol)
    {
        return id == message.msg_id && protocol == message.protocol ? &message : nullptr;
    }
};
inline ChatService service;
} // namespace fake_app
