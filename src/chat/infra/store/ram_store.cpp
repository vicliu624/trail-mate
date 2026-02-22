/**
 * @file ram_store.cpp
 * @brief RAM-based chat storage implementation
 */

#include "ram_store.h"
#include <algorithm>
#include <cstdio>

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
    ConversationId conv(msg.channel, msg.peer, msg.protocol);
    ConversationStorage& storage = getConversationStorage(conv);
    storage.messages.append(msg);
    if (msg.status == MessageStatus::Incoming)
    {
        storage.unread_count++;
    }
}

std::vector<ChatMessage> RamStore::loadRecent(const ConversationId& conv, size_t n)
{
    const ConversationStorage& storage = getConversationStorage(conv);
    std::vector<ChatMessage> result;

    size_t count = storage.messages.count();
    size_t start = (count > n) ? (count - n) : 0;

    for (size_t i = start; i < count; i++)
    {
        const ChatMessage* msg = storage.messages.get(i);
        if (msg)
        {
            result.push_back(*msg);
        }
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
        size_t count = storage.messages.count();
        if (count == 0)
        {
            continue;
        }
        const ChatMessage* last = storage.messages.get(count - 1);
        if (!last)
        {
            continue;
        }
        ConversationMeta meta;
        meta.id = conv;
        meta.preview = last->text;
        meta.last_timestamp = last->timestamp;
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
    storage.messages.clear();
    storage.unread_count = 0;
}

void RamStore::clearAll()
{
    conversations_.clear();
}

bool RamStore::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    if (msg_id == 0) return false;
    for (auto& pair : conversations_)
    {
        ConversationStorage& storage = pair.second;
        size_t count = storage.messages.count();
        for (size_t i = 0; i < count; ++i)
        {
            ChatMessage* msg = storage.messages.get(i);
            if (!msg) continue;
            if (msg->msg_id != msg_id) continue;
            if (msg->from != 0) continue; // only update outgoing messages
            msg->status = status;
            return true;
        }
    }
    return false;
}

RamStore::ConversationStorage& RamStore::getConversationStorage(const ConversationId& conv)
{
    auto it = conversations_.find(conv);
    if (it == conversations_.end())
    {
        auto result = conversations_.emplace(conv, ConversationStorage());
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
