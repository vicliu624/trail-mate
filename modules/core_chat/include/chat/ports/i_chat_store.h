/**
 * @file i_chat_store.h
 * @brief Chat storage interface
 */

#pragma once

#include "../domain/chat_types.h"
#include <vector>

namespace chat
{

/**
 * @brief Chat storage interface
 * Abstracts storage implementation (RAM, Flash, etc.)
 */
class IChatStore
{
  public:
    virtual ~IChatStore() = default;

    /**
     * @brief Append message to storage
     * @param msg Message to append
     */
    virtual void append(const ChatMessage& msg) = 0;

    /**
     * @brief Load recent messages for a conversation
     * @param conv Conversation ID
     * @param n Number of messages to load
     * @return Vector of messages (oldest first)
     */
    virtual std::vector<ChatMessage> loadRecent(const ConversationId& conv, size_t n) = 0;

    /**
     * @brief Load conversation list metadata
     * @param offset Start offset (pagination)
     * @param limit Max items to return (0 means all)
     * @param total Optional total count out-parameter
     * @return Vector of conversation meta items
     */
    virtual std::vector<ConversationMeta> loadConversationPage(size_t offset,
                                                               size_t limit,
                                                               size_t* total) = 0;

    /**
     * @brief Set unread count for conversation
     * @param conv Conversation ID
     * @param unread Unread count
     */
    virtual void setUnread(const ConversationId& conv, int unread) = 0;

    /**
     * @brief Get unread count for conversation
     * @param conv Conversation ID
     * @return Unread count
     */
    virtual int getUnread(const ConversationId& conv) const = 0;

    /**
     * @brief Clear all messages for conversation
     * @param conv Conversation ID
     */
    virtual void clearConversation(const ConversationId& conv) = 0;

    /**
     * @brief Clear all messages for all channels
     */
    virtual void clearAll() = 0;

    /**
     * @brief Update stored message status by message ID
     * @param msg_id Message ID
     * @param status New status
     * @return true if updated
     */
    virtual bool updateMessageStatus(MessageId msg_id, MessageStatus status) = 0;
};

} // namespace chat
