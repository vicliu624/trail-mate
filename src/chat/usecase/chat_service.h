/**
 * @file chat_service.h
 * @brief Chat service (use case layer)
 */

#pragma once

#include "../domain/chat_model.h"
#include "../domain/chat_types.h"
#include "../ports/i_chat_store.h"
#include "../ports/i_mesh_adapter.h"

namespace chat
{

/**
 * @brief Chat service
 * Use case layer: coordinates domain model, adapters, and storage
 */
class ChatService
{
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
     * @brief Mark conversation as read
     * @param conv Conversation ID
     */
    void markConversationRead(const ConversationId& conv);

    /**
     * @brief Resend failed message
     * @param msg_id Message ID
     * @return true if queued for resend
     */
    bool resendFailed(MessageId msg_id);

    /**
     * @brief Get recent messages for a conversation
     */
    std::vector<ChatMessage> getRecentMessages(const ConversationId& conv, size_t limit) const;
    std::vector<ConversationMeta> getConversations(size_t offset, size_t limit, size_t* total) const;

    /**
     * @brief Enable/disable in-memory model updates
     */
    void setModelEnabled(bool enabled);
    bool isModelEnabled() const { return model_enabled_; }

    /**
     * @brief Clear all stored messages and model state
     */
    void clearAllMessages();

    /**
     * @brief Process incoming messages (call from mesh task)
     */
    void processIncoming();

    /**
     * @brief Handle send result (ack/timeout)
     * @param msg_id Message ID
     * @param ok true if sent successfully
     */
    void handleSendResult(MessageId msg_id, bool ok);

    /**
     * @brief Get message by ID (for UI send status)
     */
    const ChatMessage* getMessage(MessageId msg_id) const;

    /**
     * @brief Get current channel
     */
    ChannelId getCurrentChannel() const
    {
        return current_channel_;
    }

  private:
    ChatModel& model_;
    IMeshAdapter& adapter_;
    IChatStore& store_;
    ChannelId current_channel_;
    bool model_enabled_ = true;
};

} // namespace chat
