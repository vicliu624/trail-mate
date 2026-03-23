/**
 * @file chat_service.cpp
 * @brief Chat service implementation
 */

#include "chat/usecase/chat_service.h"
#include "chat/time_utils.h"
#include <cstdio>

namespace chat
{
namespace
{
NodeId normalize_conversation_peer(NodeId peer)
{
    return (peer == 0 || peer == 0xFFFFFFFFUL) ? 0 : peer;
}
} // namespace

#ifndef CHAT_SERVICE_LOG_ENABLE
#define CHAT_SERVICE_LOG_ENABLE 0
#endif

#if CHAT_SERVICE_LOG_ENABLE
#define CHAT_SERVICE_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_SERVICE_LOG(...)
#endif

ChatService::ChatService(ChatModel& model,
                         IMeshAdapter& adapter,
                         IChatStore& store,
                         MeshProtocol active_protocol)
    : model_(model), adapter_(adapter), store_(store),
      current_channel_(ChannelId::PRIMARY),
      active_protocol_(active_protocol)
{
}

MessageId ChatService::sendText(ChannelId channel, const std::string& text, NodeId peer)
{
    return sendTextWithId(channel, text, 0, peer);
}

MessageId ChatService::sendTextWithId(ChannelId channel, const std::string& text,
                                      MessageId forced_msg_id, NodeId peer)
{
    if (text.empty())
    {
        return 0;
    }

    // Try to send via adapter first to get packet ID
    MessageId msg_id = 0;
    bool queued = adapter_.sendTextWithId(channel, text, forced_msg_id, &msg_id, peer);

    ChatMessage msg;
    msg.protocol = active_protocol_;
    msg.channel = channel;
    msg.from = 0; // Local message
    msg.peer = normalize_conversation_peer(peer);
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

bool ChatService::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    return adapter_.triggerDiscoveryAction(action);
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
        msg.protocol = active_protocol_;
        msg.channel = incoming.channel;
        msg.from = incoming.from;
        msg.peer = normalize_conversation_peer(incoming.to) == 0 ? 0 : incoming.from;
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

        for (auto* observer : incoming_message_observers_)
        {
            if (observer)
            {
                observer->onIncomingMessage(msg, &incoming.rx_meta);
            }
        }

        for (auto* observer : incoming_text_observers_)
        {
            if (observer)
            {
                observer->onIncomingText(incoming);
            }
        }
    }
}

void ChatService::addIncomingMessageObserver(IncomingMessageObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto* existing : incoming_message_observers_)
    {
        if (existing == observer)
        {
            return;
        }
    }
    incoming_message_observers_.push_back(observer);
}

void ChatService::removeIncomingMessageObserver(IncomingMessageObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto it = incoming_message_observers_.begin(); it != incoming_message_observers_.end(); ++it)
    {
        if (*it == observer)
        {
            incoming_message_observers_.erase(it);
            return;
        }
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

void ChatService::addIncomingTextObserver(IncomingTextObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto* existing : incoming_text_observers_)
    {
        if (existing == observer)
        {
            return;
        }
    }
    incoming_text_observers_.push_back(observer);
}

void ChatService::removeIncomingTextObserver(IncomingTextObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto it = incoming_text_observers_.begin(); it != incoming_text_observers_.end(); ++it)
    {
        if (*it == observer)
        {
            incoming_text_observers_.erase(it);
            return;
        }
    }
}

} // namespace chat
