/**
 * @file chat_service.h
 * @brief Chat service (use case layer)
 */

#pragma once

#include "../domain/chat_model.h"
#include "../domain/chat_types.h"
#include "../ports/i_mesh_adapter.h"
#include "../ports/i_chat_store.h"

namespace chat {

/**
 * @brief Chat service
 * Use case layer: coordinates domain model, adapters, and storage
 */
class ChatService {
public:
    ChatService(ChatModel& model, IMeshAdapter& adapter, IChatStore& store);
    
    /**
     * @brief Send text message
     * @param channel Channel ID
     * @param text Message text
     * @return Message ID if queued successfully, 0 on failure
     */
    MessageId sendText(ChannelId channel, const std::string& text, NodeId peer = 0);
    
    /**
     * @brief Switch to channel
     * @param channel Channel ID
     */
    void switchChannel(ChannelId channel);
    
    /**
     * @brief Mark channel as read
     * @param channel Channel ID
     */
    void markChannelRead(ChannelId channel);
    void markConversationRead(const ConversationId& conv);
    
    /**
     * @brief Resend failed message
     * @param msg_id Message ID
     * @return true if queued for resend
     */
    bool resendFailed(MessageId msg_id);
    
    /**
     * @brief Get unread count for channel
     */
    int getUnreadCount(ChannelId channel) const;
    
    /**
     * @brief Get recent messages for channel
     */
    std::vector<ChatMessage> getRecentMessages(ChannelId channel, size_t limit) const;
    std::vector<ChatMessage> getRecentMessages(const ConversationId& conv, size_t limit) const;
    std::vector<ConversationMeta> getConversations() const;
    
    /**
     * @brief Process incoming messages (call from mesh task)
     */
    void processIncoming();
    
    /**
     * @brief Get current channel
     */
    ChannelId getCurrentChannel() const {
        return current_channel_;
    }

private:
    ChatModel& model_;
    IMeshAdapter& adapter_;
    IChatStore& store_;
    ChannelId current_channel_;
};

} // namespace chat
