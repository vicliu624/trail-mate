/**
 * @file chat_service.cpp
 * @brief Chat service implementation
 */

#include "chat_service.h"
#include "../../sys/event_bus.h"

namespace chat {

ChatService::ChatService(ChatModel& model, IMeshAdapter& adapter, IChatStore& store)
    : model_(model), adapter_(adapter), store_(store), 
      current_channel_(ChannelId::PRIMARY) {
}

MessageId ChatService::sendText(ChannelId channel, const std::string& text, NodeId peer) {
    if (text.empty()) {
        return 0;
    }
    
    ChatMessage msg;
    msg.channel = channel;
    msg.from = 0; // Local message
    msg.peer = peer;
    msg.timestamp = millis() / 1000; // Convert to seconds
    msg.text = text;
    msg.status = MessageStatus::Queued;
    
    // Queue in model
    model_.onSendQueued(msg);
    
    // Try to send via adapter
    MessageId msg_id = 0;
    if (adapter_.sendText(channel, text, &msg_id, peer)) {
        // Update message ID if adapter provided one
        if (msg_id != 0) {
            // Find and update message (simplified - in real impl might need better tracking)
            const ChatMessage* found = model_.getMessage(msg_id);
            if (found) {
                // Message ID already set
            }
        }
    } else {
        // Send failed, mark as failed
        model_.onSendResult(msg.msg_id, false);
    }
    
    // Store message
    store_.append(msg);
    
    return msg.msg_id;
}

void ChatService::switchChannel(ChannelId channel) {
    current_channel_ = channel;
    // Could emit event here
}

void ChatService::markChannelRead(ChannelId channel) {
    model_.markRead(channel);
    store_.setUnread(channel, 0);
}

bool ChatService::resendFailed(MessageId msg_id) {
    const ChatMessage* msg = model_.getMessage(msg_id);
    if (!msg || msg->status != MessageStatus::Failed) {
        return false;
    }
    
    // Resend via adapter
    MessageId new_msg_id = 0;
    if (adapter_.sendText(msg->channel, msg->text, &new_msg_id, msg->peer)) {
        // Create new queued message
        ChatMessage resend_msg = *msg;
        resend_msg.msg_id = (new_msg_id != 0) ? new_msg_id : msg_id;
        resend_msg.status = MessageStatus::Queued;
        model_.onSendQueued(resend_msg);
        return true;
    }
    
    return false;
}

int ChatService::getUnreadCount(ChannelId channel) const {
    return model_.getUnread(channel);
}

std::vector<ChatMessage> ChatService::getRecentMessages(ChannelId channel, size_t limit) const {
    return model_.getRecent(channel, limit);
}

std::vector<ChatMessage> ChatService::getRecentMessages(const ConversationId& conv, size_t limit) const {
    return model_.getRecent(conv, limit);
}

std::vector<ConversationMeta> ChatService::getConversations() const {
    return model_.getConversations();
}

void ChatService::markConversationRead(const ConversationId& conv) {
    model_.markRead(conv);
    store_.setUnread(conv.channel, 0);
}

void ChatService::processIncoming() {
    MeshIncomingText incoming;
    while (adapter_.pollIncomingText(&incoming)) {
        // Convert to ChatMessage
        ChatMessage msg;
        msg.channel = incoming.channel;
        msg.from = incoming.from;
        msg.peer = incoming.from;
        msg.msg_id = incoming.msg_id;
        msg.timestamp = incoming.timestamp ? incoming.timestamp : (millis() / 1000);
        msg.text = incoming.text;
        msg.status = MessageStatus::Incoming;
        
        // Add to model
        model_.onIncoming(msg);
        
        // Store
        store_.append(msg);

        // Emit event for UI/feedback
        sys::EventBus::publish(new sys::ChatNewMessageEvent(static_cast<uint8_t>(msg.channel),
                                                            msg.msg_id,
                                                            msg.text.c_str()));
    }
}

} // namespace chat
