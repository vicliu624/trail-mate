/**
 * @file chat_model.cpp
 * @brief Chat domain model implementation
 */

#include "chat/domain/chat_model.h"
#include "chat/domain/conversation_name.h"
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
    ConversationId conv = conversationIdForMessage(msg);
    appendMessage(conv, msg);
}

void ChatModel::onSendQueued(const ChatMessage& msg)
{
    ConversationId conv = conversationIdForMessage(msg);

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

bool ChatModel::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    for (auto& pair : conversations_)
    {
        ConversationData& data = pair.second;
        for (size_t i = 0; i < data.messages.size(); i++)
        {
            ChatMessage* msg = &data.messages[i].message;
            if (msg && msg->msg_id == msg_id)
            {
                msg->status = status;
                failed_messages_.erase(
                    std::remove_if(failed_messages_.begin(),
                                   failed_messages_.end(),
                                   [msg_id](const ChatMessage& failed)
                                   {
                                       return failed.msg_id == msg_id;
                                   }),
                    failed_messages_.end());
                if (status == MessageStatus::Failed)
                {
                    if (failed_messages_.size() >= MAX_FAILED_MESSAGES)
                    {
                        failed_messages_.erase(failed_messages_.begin());
                    }
                    failed_messages_.push_back(*msg);
                }
                return true;
            }
        }
    }
    return false;
}

bool ChatModel::updateMessageStatusForProtocol(MessageId msg_id,
                                               MeshProtocol protocol,
                                               MessageStatus status)
{
    for (auto& pair : conversations_)
    {
        ConversationData& data = pair.second;
        for (size_t i = 0; i < data.messages.size(); i++)
        {
            ChatMessage* msg = &data.messages[i].message;
            if (!msg || msg->msg_id != msg_id || msg->protocol != protocol)
            {
                continue;
            }
            msg->status = status;
            failed_messages_.erase(
                std::remove_if(failed_messages_.begin(),
                               failed_messages_.end(),
                               [msg_id, protocol](const ChatMessage& failed)
                               {
                                   return failed.msg_id == msg_id &&
                                          failed.protocol == protocol;
                               }),
                failed_messages_.end());
            if (status == MessageStatus::Failed)
            {
                if (failed_messages_.size() >= MAX_FAILED_MESSAGES)
                {
                    failed_messages_.erase(failed_messages_.begin());
                }
                failed_messages_.push_back(*msg);
            }
            return true;
        }
    }
    return false;
}

void ChatModel::onSendResult(MessageId msg_id, bool ok)
{
    (void)updateMessageStatus(msg_id, ok ? MessageStatus::Sent : MessageStatus::Failed);
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

const ChatMessage* ChatModel::getMessageForProtocol(MessageId msg_id,
                                                    MeshProtocol protocol) const
{
    for (const auto& pair : conversations_)
    {
        const ConversationData& data = pair.second;
        for (size_t i = 0; i < data.messages.size(); i++)
        {
            const ChatMessage* msg = &data.messages[i].message;
            if (msg && msg->msg_id == msg_id && msg->protocol == protocol)
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

void ChatModel::clearConversation(const ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        return;
    }

    const size_t removed_count = it->second.messages.size();
    conversations_.erase(it);
    total_message_count_ -= std::min(total_message_count_, removed_count);
    failed_messages_.erase(
        std::remove_if(failed_messages_.begin(),
                       failed_messages_.end(),
                       [&](const ChatMessage& failed)
                       {
                           return conversationIdForMessage(failed) == conv;
                       }),
        failed_messages_.end());
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
        meta.reticulum_identity = pair.second.messages.back().message.reticulum_identity;
        meta.name = fallbackConversationName(meta.id);
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
