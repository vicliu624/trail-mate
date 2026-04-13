/**
 * @file chat_service.h
 * @brief Chat service (use case layer)
 */

#pragma once

#include "../domain/chat_model.h"
#include "../domain/chat_types.h"
#include "../ports/i_chat_store.h"
#include "../ports/i_mesh_adapter.h"
#include <vector>

namespace chat
{

/**
 * @brief Chat service
 * Use case layer: coordinates domain model, adapters, and storage
 */
class ChatService
{
  public:
    class IncomingTextObserver
    {
      public:
        virtual ~IncomingTextObserver() = default;
        virtual void onIncomingText(const MeshIncomingText& msg) = 0;
    };

    class IncomingMessageObserver
    {
      public:
        virtual ~IncomingMessageObserver() = default;
        virtual void onIncomingMessage(const ChatMessage& msg, const RxMeta* rx_meta) = 0;
    };

    class OutgoingTextObserver
    {
      public:
        virtual ~OutgoingTextObserver() = default;
        virtual void onOutgoingText(const MeshIncomingText& msg) = 0;
    };

    class IncomingDataObserver
    {
      public:
        virtual ~IncomingDataObserver() = default;
        virtual void onIncomingData(const MeshIncomingData& msg) = 0;
    };

    ChatService(ChatModel& model,
                IMeshAdapter& adapter,
                IChatStore& store,
                MeshProtocol active_protocol = MeshProtocol::Meshtastic);

    /**
     * @brief Send text message
     * @param channel Channel ID
     * @param text Message text
     * @return Message ID if queued successfully, 0 on failure
     */
    MessageId sendText(ChannelId channel, const std::string& text, NodeId peer = 0);
    MessageId sendTextWithId(ChannelId channel, const std::string& text,
                             MessageId forced_msg_id, NodeId peer = 0);

    /**
     * @brief Trigger protocol discovery action (if supported by active adapter)
     */
    bool triggerDiscoveryAction(MeshDiscoveryAction action);

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
    int getTotalUnread() const;

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
    void flushStore();

    void addIncomingTextObserver(IncomingTextObserver* observer);
    void removeIncomingTextObserver(IncomingTextObserver* observer);

    void addIncomingMessageObserver(IncomingMessageObserver* observer);
    void removeIncomingMessageObserver(IncomingMessageObserver* observer);

    void addOutgoingTextObserver(OutgoingTextObserver* observer);
    void removeOutgoingTextObserver(OutgoingTextObserver* observer);

    void addIncomingDataObserver(IncomingDataObserver* observer);
    void removeIncomingDataObserver(IncomingDataObserver* observer);

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

    void setActiveProtocol(MeshProtocol protocol)
    {
        active_protocol_ = protocol;
    }

    MeshProtocol getActiveProtocol() const
    {
        return active_protocol_;
    }

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
    MeshProtocol active_protocol_ = MeshProtocol::Meshtastic;

    std::vector<IncomingTextObserver*> incoming_text_observers_;
    std::vector<IncomingMessageObserver*> incoming_message_observers_;
    std::vector<OutgoingTextObserver*> outgoing_text_observers_;
    std::vector<IncomingDataObserver*> incoming_data_observers_;
};

} // namespace chat
