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

    MessageId msg_id = 0;
    bool queued = adapter_.sendTextWithId(channel, text, forced_msg_id, &msg_id, peer);

    ChatMessage msg;
    msg.protocol = active_protocol_;
    msg.channel = channel;
    msg.from = 0;
    msg.peer = normalize_conversation_peer(peer);
    msg.msg_id = msg_id;
    msg.timestamp = now_message_timestamp();
    msg.text = text;
    msg.status = queued ? MessageStatus::Queued : MessageStatus::Failed;

    if (model_enabled_)
    {
        model_.onSendQueued(msg);
        if (!queued && msg_id != 0)
        {
            model_.onSendResult(msg_id, false);
        }
    }

    store_.append(msg);

    if (queued && msg_id != 0)
    {
        MeshIncomingText outgoing{};
        outgoing.channel = channel;
        outgoing.from = adapter_.getNodeId();
        outgoing.to = (peer != 0) ? peer : 0xFFFFFFFFUL;
        outgoing.msg_id = msg_id;
        outgoing.timestamp = msg.timestamp;
        outgoing.text = text;
        outgoing.hop_limit = 0;
        outgoing.encrypted = false;

        for (auto* observer : outgoing_text_observers_)
        {
            if (observer)
            {
                observer->onOutgoingText(outgoing);
            }
        }
    }

    return msg.msg_id;
}

bool ChatService::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    return adapter_.triggerDiscoveryAction(action);
}

void ChatService::switchChannel(ChannelId channel)
{
    current_channel_ = channel;
}

bool ChatService::resendFailed(MessageId msg_id)
{
    ChatMessage msg;
    if (const ChatMessage* model_msg = model_.getMessage(msg_id))
    {
        msg = *model_msg;
    }
    else if (!store_.getMessage(msg_id, &msg))
    {
        return false;
    }

    if (msg.status != MessageStatus::Failed)
    {
        return false;
    }

    return sendText(msg.channel, msg.text, msg.peer) != 0;
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
    MeshIncomingText incoming_text;
    while (adapter_.pollIncomingText(&incoming_text))
    {
        ChatMessage msg;
        msg.protocol = active_protocol_;
        msg.channel = incoming_text.channel;
        msg.from = incoming_text.from;
        msg.peer = normalize_conversation_peer(incoming_text.to) == 0 ? 0 : incoming_text.from;
        msg.msg_id = incoming_text.msg_id;
        msg.timestamp = now_message_timestamp();
        msg.text = incoming_text.text;
        msg.status = MessageStatus::Incoming;

        CHAT_SERVICE_LOG("[ChatService] incoming text ch=%u from=%08lX to=%08lX peer=%08lX ts=%lu len=%u\n",
                         static_cast<unsigned>(msg.channel),
                         static_cast<unsigned long>(msg.from),
                         static_cast<unsigned long>(incoming_text.to),
                         static_cast<unsigned long>(msg.peer),
                         static_cast<unsigned long>(msg.timestamp),
                         static_cast<unsigned>(msg.text.size()));

        if (model_enabled_)
        {
            model_.onIncoming(msg);
        }

        store_.append(msg);

        for (auto* observer : incoming_message_observers_)
        {
            if (observer)
            {
                observer->onIncomingMessage(msg, &incoming_text.rx_meta);
            }
        }

        for (auto* observer : incoming_text_observers_)
        {
            if (observer)
            {
                observer->onIncomingText(incoming_text);
            }
        }
    }

    MeshIncomingData incoming_data;
    while (adapter_.pollIncomingData(&incoming_data))
    {
        CHAT_SERVICE_LOG("[ChatService] incoming data ch=%u from=%08lX to=%08lX pkt=%08lX port=%u len=%u\n",
                         static_cast<unsigned>(incoming_data.channel),
                         static_cast<unsigned long>(incoming_data.from),
                         static_cast<unsigned long>(incoming_data.to),
                         static_cast<unsigned long>(incoming_data.packet_id),
                         static_cast<unsigned>(incoming_data.portnum),
                         static_cast<unsigned>(incoming_data.payload.size()));

        for (auto* observer : incoming_data_observers_)
        {
            if (observer)
            {
                observer->onIncomingData(incoming_data);
            }
        }
    }
}

void ChatService::flushStore()
{
    store_.flush();
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

void ChatService::addOutgoingTextObserver(OutgoingTextObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto* existing : outgoing_text_observers_)
    {
        if (existing == observer)
        {
            return;
        }
    }
    outgoing_text_observers_.push_back(observer);
}

void ChatService::removeOutgoingTextObserver(OutgoingTextObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto it = outgoing_text_observers_.begin(); it != outgoing_text_observers_.end(); ++it)
    {
        if (*it == observer)
        {
            outgoing_text_observers_.erase(it);
            return;
        }
    }
}

void ChatService::addIncomingDataObserver(IncomingDataObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto* existing : incoming_data_observers_)
    {
        if (existing == observer)
        {
            return;
        }
    }
    incoming_data_observers_.push_back(observer);
}

void ChatService::removeIncomingDataObserver(IncomingDataObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto it = incoming_data_observers_.begin(); it != incoming_data_observers_.end(); ++it)
    {
        if (*it == observer)
        {
            incoming_data_observers_.erase(it);
            return;
        }
    }
}

void ChatService::handleSendResult(MessageId msg_id, bool ok)
{
    if (msg_id == 0)
    {
        return;
    }
    if (model_enabled_)
    {
        model_.onSendResult(msg_id, ok);
    }
    store_.updateMessageStatus(msg_id, ok ? MessageStatus::Sent : MessageStatus::Failed);
}

const ChatMessage* ChatService::getMessage(MessageId msg_id) const
{
    if (const ChatMessage* msg = model_.getMessage(msg_id))
    {
        return msg;
    }
    if (store_.getMessage(msg_id, &store_lookup_cache_))
    {
        return &store_lookup_cache_;
    }
    return nullptr;
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
