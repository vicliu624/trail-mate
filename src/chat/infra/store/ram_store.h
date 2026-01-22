/**
 * @file ram_store.h
 * @brief RAM-based chat storage
 */

#pragma once

#include "../../../sys/ringbuf.h"
#include "../../domain/chat_types.h"
#include "../../ports/i_chat_store.h"
#include <map>

namespace chat
{

/**
 * @brief RAM-based chat storage
 * Uses ring buffers for message storage
 */
class RamStore : public IChatStore
{
  public:
    static constexpr size_t MAX_MESSAGES_PER_CONV = 100;

    RamStore();
    virtual ~RamStore();

    void append(const ChatMessage& msg) override;
    std::vector<ChatMessage> loadRecent(const ConversationId& conv, size_t n) override;
    std::vector<ConversationMeta> loadConversationPage(size_t offset,
                                                       size_t limit,
                                                       size_t* total) override;
    void setUnread(const ConversationId& conv, int unread) override;
    int getUnread(const ConversationId& conv) const override;
    void clearConversation(const ConversationId& conv) override;
    void clearAll() override;
    bool updateMessageStatus(MessageId msg_id, MessageStatus status) override;

  private:
    struct ConversationStorage
    {
        sys::RingBuffer<ChatMessage, MAX_MESSAGES_PER_CONV> messages;
        int unread_count;

        ConversationStorage() : unread_count(0) {}
    };

    std::map<ConversationId, ConversationStorage> conversations_;

    ConversationStorage& getConversationStorage(const ConversationId& conv);
    const ConversationStorage& getConversationStorage(const ConversationId& conv) const;
};

} // namespace chat
