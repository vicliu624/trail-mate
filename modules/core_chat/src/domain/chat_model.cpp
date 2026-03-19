/**
 * @file chat_model.cpp
 * @brief Chat domain model implementation
 */

#include "chat/domain/chat_model.h"
#include <algorithm>
#include <cstdio>

namespace chat
{

ChatModel::ChatModel() : policy_(ChatPolicy::defaults()), next_msg_id_(1) {}

ChatModel::~ChatModel()
{
}

void ChatModel::onIncoming(const ChatMessage& msg)
{
    ConversationId conv(msg.channel, msg.peer, msg.protocol);
    appendMessage(conv, msg);
}

void ChatModel::onSendQueued(const ChatMessage& msg)
{
    ConversationId conv(msg.channel, msg.peer, msg.protocol);

    ChatMessage copy = msg;
    if (copy.msg_id == 0)
    {
        copy.msg_id = next_msg_id_++;
    }
    if (copy.status != MessageStatus::Failed)
    {
        copy.status = MessageStatus::Queued;
    }

    appendMessage(conv, copy);
}

void ChatModel::onSendResult(MessageId msg_id, bool ok)
{
    // Find message in all conversations
    for (auto& pair : conversations_)
    {
        ConversationData& data = pair.second;
        for (size_t i = 0; i < data.messages.size(); i++)
        {
            ChatMessage* msg = &data.messages[i].message;
            if (msg && msg->msg_id == msg_id)
            {
                msg->status = ok ? MessageStatus::Sent : MessageStatus::Failed;
                if (!ok)
                {
                    if (failed_messages_.size() >= MAX_FAILED_MESSAGES)
                    {
                        failed_messages_.erase(failed_messages_.begin());
                    }
                    failed_messages_.push_back(*msg);
                }
                return;
            }
        }
    }
}

int ChatModel::getUnread(const ConversationId& conv) const
{
    const ConversationData& data = getConvData(conv);
    return data.unread_count;
}

void ChatModel::markRead(const ConversationId& conv)
{
    ConversationData& data = getConvData(conv);
    data.unread_count = 0;
}

std::vector<ChatMessage> ChatModel::getRecent(const ConversationId& conv, size_t limit) const
{
    const ConversationData& data = getConvData(conv);
    std::vector<ChatMessage> result;

    size_t count = data.messages.size();
    size_t start = (count > limit) ? (count - limit) : 0;

    for (size_t i = start; i < count; i++)
    {
        result.push_back(data.messages[i].message);
    }

    return result;
}

std::vector<ChatMessage> ChatModel::getFailedMessages() const
{
    std::vector<ChatMessage> result;
    size_t count = failed_messages_.size();

    for (size_t i = 0; i < count; i++)
    {
        result.push_back(failed_messages_[i]);
    }

    return result;
}

const ChatMessage* ChatModel::getMessage(MessageId msg_id) const
{
    for (const auto& pair : conversations_)
    {
        const ConversationData& data = pair.second;
        for (size_t i = 0; i < data.messages.size(); i++)
        {
            const ChatMessage* msg = &data.messages[i].message;
            if (msg && msg->msg_id == msg_id)
            {
                return msg;
            }
        }
    }
    return nullptr;
}

void ChatModel::clearAll()
{
    conversations_.clear();
    failed_messages_.clear();
    total_message_count_ = 0;
    next_sequence_ = 1;
}

std::vector<ConversationMeta> ChatModel::getConversations() const
{
    std::vector<ConversationMeta> list;
    list.reserve(conversations_.size());

    for (const auto& pair : conversations_)
    {
        if (pair.second.last_ts == 0)
        {
            continue; // skip empty conversations
        }
        ConversationMeta meta;
        meta.id = pair.first;
        meta.preview = pair.second.preview;
        meta.last_timestamp = pair.second.last_ts;
        meta.unread = pair.second.unread_count;
        // Naming: broadcast vs peer short id
        if (pair.first.peer == 0)
        {
            meta.name = "Broadcast";
        }
        else
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04lX", static_cast<unsigned long>(pair.first.peer & 0xFFFF));
            meta.name = buf;
        }
        list.push_back(meta);
    }

    std::sort(list.begin(), list.end(), [](const ConversationMeta& a, const ConversationMeta& b)
              { return a.last_timestamp > b.last_timestamp; });

    return list;
}

ChatModel::ConversationData& ChatModel::getConvData(const ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        auto result = conversations_.emplace(conv, ConversationData());
        return result.first->second;
    }
    return it->second;
}

const ChatModel::ConversationData& ChatModel::getConvData(const ConversationId& conv) const
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        static ConversationData empty;
        return empty;
    }
    return it->second;
}

void ChatModel::appendMessage(const ConversationId& conv, const ChatMessage& msg)
{
    if (total_message_count_ >= MAX_MESSAGES_TOTAL)
    {
        evictOldestMessage();
    }

    ConversationData& data = getConvData(conv);
    StoredMessageEntry entry;
    entry.message = msg;
    entry.sequence = next_sequence_++;
    data.messages.push_back(entry);
    total_message_count_++;
    refreshConversationMeta(data);

    if (msg.status == MessageStatus::Incoming && !data.muted)
    {
        data.unread_count++;
    }
}

void ChatModel::evictOldestMessage()
{
    auto oldest_it = conversations_.end();
    size_t oldest_index = 0;
    uint32_t oldest_sequence = 0;
    bool found = false;

    for (auto it = conversations_.begin(); it != conversations_.end(); ++it)
    {
        auto& messages = it->second.messages;
        for (size_t index = 0; index < messages.size(); ++index)
        {
            if (!found || messages[index].sequence < oldest_sequence)
            {
                oldest_it = it;
                oldest_index = index;
                oldest_sequence = messages[index].sequence;
                found = true;
            }
        }
    }

    if (!found)
    {
        total_message_count_ = 0;
        return;
    }

    ConversationData& data = oldest_it->second;
    const ChatMessage removed = data.messages[oldest_index].message;
    data.messages.erase(data.messages.begin() + static_cast<long>(oldest_index));
    if (removed.status == MessageStatus::Incoming && data.unread_count > 0)
    {
        data.unread_count--;
    }
    refreshConversationMeta(data);
    if (data.messages.empty())
    {
        conversations_.erase(oldest_it);
    }
    if (total_message_count_ > 0)
    {
        total_message_count_--;
    }
}

void ChatModel::refreshConversationMeta(ConversationData& data)
{
    if (data.messages.empty())
    {
        data.preview.clear();
        data.last_ts = 0;
        data.unread_count = 0;
        return;
    }

    const ChatMessage& newest = data.messages.back().message;
    data.preview = newest.text;
    data.last_ts = newest.timestamp;
}

} // namespace chat
