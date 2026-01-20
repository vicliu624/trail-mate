/**
 * @file chat_model.cpp
 * @brief Chat domain model implementation
 */

#include "chat_model.h"
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
    ConversationId conv(msg.channel, msg.peer);
    ConversationData& data = getConvData(conv);

    data.messages.append(msg);
    data.preview = msg.text;
    data.last_ts = msg.timestamp;

    if (!data.muted)
    {
        data.unread_count++;
    }
}

void ChatModel::onSendQueued(const ChatMessage& msg)
{
    ConversationId conv(msg.channel, msg.peer);
    ConversationData& data = getConvData(conv);

    ChatMessage copy = msg;
    if (copy.msg_id == 0)
    {
        copy.msg_id = next_msg_id_++;
    }
    if (copy.status != MessageStatus::Failed)
    {
        copy.status = MessageStatus::Queued;
    }

    data.messages.append(copy);
    data.preview = copy.text;
    data.last_ts = copy.timestamp;
}

void ChatModel::onSendResult(MessageId msg_id, bool ok)
{
    // Find message in all conversations
    for (auto& pair : conversations_)
    {
        ConversationData& data = pair.second;
        for (size_t i = 0; i < data.messages.count(); i++)
        {
            ChatMessage* msg = data.messages.get(i);
            if (msg && msg->msg_id == msg_id)
            {
                msg->status = ok ? MessageStatus::Sent : MessageStatus::Failed;
                if (!ok)
                {
                    failed_messages_.append(*msg);
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

    size_t count = data.messages.count();
    size_t start = (count > limit) ? (count - limit) : 0;

    for (size_t i = start; i < count; i++)
    {
        const ChatMessage* msg = data.messages.get(i);
        if (msg)
        {
            result.push_back(*msg);
        }
    }

    return result;
}

std::vector<ChatMessage> ChatModel::getFailedMessages() const
{
    std::vector<ChatMessage> result;
    size_t count = failed_messages_.count();

    for (size_t i = 0; i < count; i++)
    {
        const ChatMessage* msg = failed_messages_.get(i);
        if (msg)
        {
            result.push_back(*msg);
        }
    }

    return result;
}

const ChatMessage* ChatModel::getMessage(MessageId msg_id) const
{
    for (const auto& pair : conversations_)
    {
        const ConversationData& data = pair.second;
        for (size_t i = 0; i < data.messages.count(); i++)
        {
            const ChatMessage* msg = data.messages.get(i);
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

} // namespace chat
