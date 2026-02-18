/**
 * @file chat_service.cpp
 * @brief Chat service implementation
 */

#include "chat_service.h"
#include "../../sys/event_bus.h"
#include "../time_utils.h"

namespace chat
{

#ifndef CHAT_SERVICE_LOG_ENABLE
#define CHAT_SERVICE_LOG_ENABLE 0
#endif

#if CHAT_SERVICE_LOG_ENABLE
#define CHAT_SERVICE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_SERVICE_LOG(...)
#endif

ChatService::ChatService(ChatModel& model, IMeshAdapter& adapter, IChatStore& store)
    : model_(model), adapter_(adapter), store_(store),
      current_channel_(ChannelId::PRIMARY)
{
}

MessageId ChatService::sendText(ChannelId channel, const std::string& text, NodeId peer)
{
    if (text.empty())
    {
        return 0;
    }

    // Try to send via adapter first to get packet ID
    MessageId msg_id = 0;
    bool queued = adapter_.sendText(channel, text, &msg_id, peer);

    ChatMessage msg;
    msg.channel = channel;
    msg.from = 0; // Local message
    msg.peer = peer;
    msg.msg_id = msg_id;
    msg.timestamp = now_message_timestamp();
    msg.text = text;
    msg.status = queued ? MessageStatus::Queued : MessageStatus::Failed;

    // Queue in model (if enabled)
    if (model_enabled_)
    {
        model_.onSendQueued(msg);
        if (!queued && msg_id != 0)
        {
            model_.onSendResult(msg_id, false);
        }
    }

    // Store message
    store_.append(msg);

    return msg.msg_id;
}

void ChatService::switchChannel(ChannelId channel)
{
    current_channel_ = channel;
    // Could emit event here
}

bool ChatService::resendFailed(MessageId msg_id)
{
    const ChatMessage* msg = model_.getMessage(msg_id);
    if (!msg || msg->status != MessageStatus::Failed)
    {
        return false;
    }

    // Resend via adapter
    MessageId new_msg_id = 0;
    if (adapter_.sendText(msg->channel, msg->text, &new_msg_id, msg->peer))
    {
        // Create new queued message
        ChatMessage resend_msg = *msg;
        resend_msg.msg_id = (new_msg_id != 0) ? new_msg_id : msg_id;
        resend_msg.status = MessageStatus::Queued;
        model_.onSendQueued(resend_msg);
        return true;
    }

    return false;
}

std::vector<ChatMessage> ChatService::getRecentMessages(const ConversationId& conv, size_t limit) const
{
    return store_.loadRecent(conv, limit);
}

std::vector<ConversationMeta> ChatService::getConversations(size_t offset,
                                                            size_t limit,
                                                            size_t* total) const
{
    return store_.loadConversationPage(offset, limit, total);
}

int ChatService::getTotalUnread() const
{
    size_t total = 0;
    auto convs = store_.loadConversationPage(0, 0, &total);
    int sum = 0;
    for (const auto& conv : convs)
    {
        sum += conv.unread;
    }
    return sum;
}

void ChatService::clearAllMessages()
{
    model_.clearAll();
    store_.clearAll();
}

void ChatService::markConversationRead(const ConversationId& conv)
{
    model_.markRead(conv);
    store_.setUnread(conv, 0);
}

void ChatService::processIncoming()
{
    MeshIncomingText incoming;
    while (adapter_.pollIncomingText(&incoming))
    {
        // Convert to ChatMessage
        ChatMessage msg;
        msg.channel = incoming.channel;
        msg.from = incoming.from;
        if (incoming.to == 0xFFFFFFFF)
        {
            msg.peer = 0;
        }
        else
        {
            msg.peer = incoming.from;
        }
        msg.msg_id = incoming.msg_id;
        // Use local receive time to avoid sender clock skew.
        msg.timestamp = now_message_timestamp();
        msg.text = incoming.text;
        msg.status = MessageStatus::Incoming;

        CHAT_SERVICE_LOG("[ChatService] incoming ch=%u from=%08lX to=%08lX peer=%08lX ts=%lu len=%u\n",
                         static_cast<unsigned>(msg.channel),
                         static_cast<unsigned long>(msg.from),
                         static_cast<unsigned long>(incoming.to),
                         static_cast<unsigned long>(msg.peer),
                         static_cast<unsigned long>(msg.timestamp),
                         static_cast<unsigned>(msg.text.size()));

        // Add to model (if enabled)
        if (model_enabled_)
        {
            model_.onIncoming(msg);
        }

        // Store
        store_.append(msg);

        // Emit event for UI/feedback
        sys::EventBus::publish(new sys::ChatNewMessageEvent(static_cast<uint8_t>(msg.channel),
                                                            msg.msg_id,
                                                            msg.text.c_str(),
                                                            &incoming.rx_meta));
    }
}

void ChatService::handleSendResult(MessageId msg_id, bool ok)
{
    if (msg_id == 0) return;
    if (model_enabled_)
    {
        model_.onSendResult(msg_id, ok);
    }
    store_.updateMessageStatus(msg_id, ok ? MessageStatus::Sent : MessageStatus::Failed);
}

const ChatMessage* ChatService::getMessage(MessageId msg_id) const
{
    return model_.getMessage(msg_id);
}

void ChatService::setModelEnabled(bool enabled)
{
    if (model_enabled_ == enabled)
    {
        return;
    }
    model_enabled_ = enabled;
    if (!model_enabled_)
    {
        model_.clearAll();
    }
}

} // namespace chat
