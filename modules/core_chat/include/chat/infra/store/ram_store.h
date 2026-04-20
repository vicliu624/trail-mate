/**
 * @file ram_store.h
 * @brief RAM-based chat storage
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/ports/i_chat_store.h"
#include <map>
#include <vector>

namespace chat
{

/**
 * @brief RAM-based chat storage
 * Uses ring buffers for message storage
 */
class RamStore : public IChatStore
{
  public:
    static constexpr size_t MAX_MESSAGES_TOTAL = 20;

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
    bool getMessage(MessageId msg_id, ChatMessage* out) const override;

  private:
    struct StoredMessageEntry
    {
        ChatMessage message;
        uint32_t sequence = 0;
    };

    struct ConversationStorage
    {
        std::vector<StoredMessageEntry> messages;
        int unread_count;

        ConversationStorage() : unread_count(0) {}
    };

    std::map<ConversationId, ConversationStorage> conversations_;
    uint32_t next_sequence_ = 1;
    size_t total_message_count_ = 0;

    ConversationStorage& getConversationStorage(const ConversationId& conv);
    const ConversationStorage& getConversationStorage(const ConversationId& conv) const;
    void evictOldestMessage();
};

} // namespace chat
