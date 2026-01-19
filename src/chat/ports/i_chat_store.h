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
     * @brief Load recent messages for channel
     * @param channel Channel ID
     * @param n Number of messages to load
     * @return Vector of messages (oldest first)
     */
    virtual std::vector<ChatMessage> loadRecent(ChannelId channel, size_t n) = 0;

    /**
     * @brief Set unread count for channel
     * @param channel Channel ID
     * @param unread Unread count
     */
    virtual void setUnread(ChannelId channel, int unread) = 0;

    /**
     * @brief Get unread count for channel
     * @param channel Channel ID
     * @return Unread count
     */
    virtual int getUnread(ChannelId channel) const = 0;

    /**
     * @brief Clear all messages for channel
     * @param channel Channel ID
     */
    virtual void clearChannel(ChannelId channel) = 0;
};

} // namespace chat
