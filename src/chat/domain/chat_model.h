/**
 * @file chat_model.h
 * @brief Chat domain model
 */

#pragma once

#include "../sys/ringbuf.h"
#include "chat_policy.h"
#include "chat_types.h"
#include <map>
#include <vector>

namespace chat
{

/**
 * @brief Chat domain model
 * Manages chat state: messages, unread counts, channels
 */
class ChatModel
{
  public:
    static constexpr size_t MAX_MESSAGES_PER_CONV = 50;
    static constexpr size_t MAX_FAILED_MESSAGES = 5;

    ChatModel();
    ~ChatModel();

    void onIncoming(const ChatMessage& msg);
    void onSendQueued(const ChatMessage& msg);

    /**
     * @brief Handle send result
     * @param msg_id Message ID
     * @param ok true if sent successfully
     */
    void onSendResult(MessageId msg_id, bool ok);

    int getUnread(const ConversationId& conv) const;
    void markRead(const ConversationId& conv);
    std::vector<ChatMessage> getRecent(const ConversationId& conv, size_t limit) const;

    /**
     * @brief Get conversation list metadata (sorted by last_timestamp desc)
     */
    std::vector<ConversationMeta> getConversations() const;

    /**
     * @brief Get failed messages
     */
    std::vector<ChatMessage> getFailedMessages() const;

    /**
     * @brief Get message by ID
     */
    const ChatMessage* getMessage(MessageId msg_id) const;

    /**
     * @brief Clear all conversations and failed messages
     */
    void clearAll();

    /**
     * @brief Set policy
     */
    void setPolicy(const ChatPolicy& policy)
    {
        policy_ = policy;
    }

    /**
     * @brief Get current policy
     */
    const ChatPolicy& getPolicy() const
    {
        return policy_;
    }

  private:
    struct ConversationData
    {
        sys::RingBuffer<ChatMessage, MAX_MESSAGES_PER_CONV> messages;
        int unread_count;
        uint32_t last_ts;
        std::string preview;
        bool muted;

        ConversationData() : unread_count(0), last_ts(0), muted(false) {}
    };

    std::map<ConversationId, ConversationData> conversations_;
    sys::RingBuffer<ChatMessage, MAX_FAILED_MESSAGES> failed_messages_;
    ChatPolicy policy_;
    MessageId next_msg_id_;

    ConversationData& getConvData(const ConversationId& conv);
    const ConversationData& getConvData(const ConversationId& conv) const;
};

} // namespace chat
