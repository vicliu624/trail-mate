/**
 * @file ram_store.h
 * @brief RAM-based chat storage
 */

#pragma once

#include "../../ports/i_chat_store.h"
#include "../../domain/chat_types.h"
#include "../../../sys/ringbuf.h"
#include <map>

namespace chat {

/**
 * @brief RAM-based chat storage
 * Uses ring buffers for message storage
 */
class RamStore : public IChatStore {
public:
    static constexpr size_t MAX_MESSAGES_PER_CHANNEL = 20;  // Reduced to prevent stack overflow
    
    RamStore();
    virtual ~RamStore();
    
    void append(const ChatMessage& msg) override;
    std::vector<ChatMessage> loadRecent(ChannelId channel, size_t n) override;
    void setUnread(ChannelId channel, int unread) override;
    int getUnread(ChannelId channel) const override;
    void clearChannel(ChannelId channel) override;

private:
    struct ChannelStorage {
        sys::RingBuffer<ChatMessage, MAX_MESSAGES_PER_CHANNEL> messages;
        int unread_count;
        
        ChannelStorage() : unread_count(0) {}
    };
    
    std::map<ChannelId, ChannelStorage> channels_;
    
    ChannelStorage& getChannelStorage(ChannelId channel);
    const ChannelStorage& getChannelStorage(ChannelId channel) const;
};

} // namespace chat
