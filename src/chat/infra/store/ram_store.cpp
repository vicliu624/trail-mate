/**
 * @file ram_store.cpp
 * @brief RAM-based chat storage implementation
 */

#include "ram_store.h"

namespace chat {

RamStore::RamStore() {
    // Initialize channels (use emplace to avoid temporary on stack)
    channels_.emplace(ChannelId::PRIMARY, ChannelStorage());
    channels_.emplace(ChannelId::SECONDARY, ChannelStorage());
}

RamStore::~RamStore() {
}

void RamStore::append(const ChatMessage& msg) {
    ChannelStorage& storage = getChannelStorage(msg.channel);
    storage.messages.append(msg);
}

std::vector<ChatMessage> RamStore::loadRecent(ChannelId channel, size_t n) {
    const ChannelStorage& storage = getChannelStorage(channel);
    std::vector<ChatMessage> result;
    
    size_t count = storage.messages.count();
    size_t start = (count > n) ? (count - n) : 0;
    
    for (size_t i = start; i < count; i++) {
        const ChatMessage* msg = storage.messages.get(i);
        if (msg) {
            result.push_back(*msg);
        }
    }
    
    return result;
}

void RamStore::setUnread(ChannelId channel, int unread) {
    ChannelStorage& storage = getChannelStorage(channel);
    storage.unread_count = unread;
}

int RamStore::getUnread(ChannelId channel) const {
    const ChannelStorage& storage = getChannelStorage(channel);
    return storage.unread_count;
}

void RamStore::clearChannel(ChannelId channel) {
    ChannelStorage& storage = getChannelStorage(channel);
    storage.messages.clear();
    storage.unread_count = 0;
}

RamStore::ChannelStorage& RamStore::getChannelStorage(ChannelId channel) {
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        // Use emplace to avoid temporary on stack
        auto result = channels_.emplace(channel, ChannelStorage());
        return result.first->second;
    }
    return it->second;
}

const RamStore::ChannelStorage& RamStore::getChannelStorage(ChannelId channel) const {
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        static ChannelStorage empty;
        return empty;
    }
    return it->second;
}

} // namespace chat
