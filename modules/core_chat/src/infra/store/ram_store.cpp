/**
 * @file ram_store.cpp
 * @brief RAM-based chat storage implementation
 */

#include "chat/infra/store/ram_store.h"
#include <algorithm>
#include <cstdio>
#include <tuple>
#include <utility>

namespace chat
{

RamStore::RamStore()
{
}

RamStore::~RamStore()
{
}

void RamStore::append(const ChatMessage& msg)
{
    if (total_message_count_ >= MAX_MESSAGES_TOTAL)
    {
        evictOldestMessage();
    }

    ConversationId conv(msg.channel, msg.peer, msg.protocol);
    ConversationStorage& storage = getConversationStorage(conv);
    StoredMessageEntry entry;
    entry.message = msg;
    entry.sequence = next_sequence_++;
    storage.messages.push_back(entry);
    total_message_count_++;
    if (msg.status == MessageStatus::Incoming)
    {
        storage.unread_count++;
    }
}

std::vector<ChatMessage> RamStore::loadRecent(const ConversationId& conv, size_t n)
{
    const ConversationStorage& storage = getConversationStorage(conv);
    std::vector<ChatMessage> result;

    size_t count = storage.messages.size();
    size_t start = (count > n) ? (count - n) : 0;

    for (size_t i = start; i < count; i++)
    {
        result.push_back(storage.messages[i].message);
    }

    return result;
}

std::vector<ConversationMeta> RamStore::loadConversationPage(size_t offset,
                                                             size_t limit,
                                                             size_t* total)
{
    std::vector<ConversationMeta> list;
    list.reserve(conversations_.size());
    for (const auto& pair : conversations_)
    {
        const ConversationId& conv = pair.first;
        const ConversationStorage& storage = pair.second;
        size_t count = storage.messages.size();
        if (count == 0)
        {
            continue;
        }
        ConversationMeta meta;
        meta.id = conv;
        meta.preview = storage.messages.back().message.text;
        meta.last_timestamp = storage.messages.back().message.timestamp;
        meta.unread = storage.unread_count;
        if (conv.peer == 0)
        {
            meta.name = "Broadcast";
        }
        else
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04lX",
                     static_cast<unsigned long>(conv.peer & 0xFFFF));
            meta.name = buf;
        }
        list.push_back(meta);
    }

    std::sort(list.begin(), list.end(),
              [](const ConversationMeta& a, const ConversationMeta& b)
              {
                  return a.last_timestamp > b.last_timestamp;
              });
    if (total)
    {
        *total = list.size();
    }
    if (limit != 0 && offset < list.size())
    {
        size_t end = offset + limit;
        if (end > list.size())
        {
            end = list.size();
        }
        return std::vector<ConversationMeta>(list.begin() + static_cast<long>(offset),
                                             list.begin() + static_cast<long>(end));
    }
    if (offset >= list.size())
    {
        return {};
    }
    if (offset > 0)
    {
        return std::vector<ConversationMeta>(list.begin() + static_cast<long>(offset), list.end());
    }
    return list;
}

void RamStore::setUnread(const ConversationId& conv, int unread)
{
    ConversationStorage& storage = getConversationStorage(conv);
    storage.unread_count = unread;
}

int RamStore::getUnread(const ConversationId& conv) const
{
    const ConversationStorage& storage = getConversationStorage(conv);
    return storage.unread_count;
}

void RamStore::clearConversation(const ConversationId& conv)
{
    ConversationStorage& storage = getConversationStorage(conv);
    total_message_count_ -= std::min(total_message_count_, storage.messages.size());
    storage.messages.clear();
    storage.unread_count = 0;
    conversations_.erase(conv);
}

void RamStore::clearAll()
{
    conversations_.clear();
    total_message_count_ = 0;
    next_sequence_ = 1;
}

bool RamStore::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    if (msg_id == 0) return false;
    for (auto& pair : conversations_)
    {
        ConversationStorage& storage = pair.second;
        size_t count = storage.messages.size();
        for (size_t i = 0; i < count; ++i)
        {
            ChatMessage* msg = &storage.messages[i].message;
            if (!msg) continue;
            if (msg->msg_id != msg_id) continue;
            if (msg->from != 0) continue; // only update outgoing messages
            msg->status = status;
            return true;
        }
    }
    return false;
}

bool RamStore::getMessage(MessageId msg_id, ChatMessage* out) const
{
    if (msg_id == 0)
    {
        return false;
    }

    for (const auto& pair : conversations_)
    {
        const ConversationStorage& storage = pair.second;
        for (const auto& entry : storage.messages)
        {
            if (entry.message.msg_id != msg_id)
            {
                continue;
            }
            if (out)
            {
                *out = entry.message;
            }
            return true;
        }
    }
    return false;
}

void RamStore::evictOldestMessage()
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

    ConversationStorage& storage = oldest_it->second;
    const ChatMessage removed = storage.messages[oldest_index].message;
    storage.messages.erase(storage.messages.begin() + static_cast<long>(oldest_index));
    if (removed.status == MessageStatus::Incoming && storage.unread_count > 0)
    {
        storage.unread_count--;
    }
    if (storage.messages.empty())
    {
        conversations_.erase(oldest_it);
    }
    if (total_message_count_ > 0)
    {
        total_message_count_--;
    }
}

RamStore::ConversationStorage& RamStore::getConversationStorage(const ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        auto result = conversations_.emplace(std::piecewise_construct,
                                             std::forward_as_tuple(conv),
                                             std::forward_as_tuple());
        return result.first->second;
    }
    return it->second;
}

const RamStore::ConversationStorage& RamStore::getConversationStorage(const ConversationId& conv) const
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        static ConversationStorage empty;
        return empty;
    }
    return it->second;
}

} // namespace chat
