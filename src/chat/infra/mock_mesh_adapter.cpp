/**
 * @file mock_mesh_adapter.cpp
 * @brief Mock mesh adapter implementation
 */

#include "mock_mesh_adapter.h"
#include <Arduino.h>

namespace chat {

MockMeshAdapter::MockMeshAdapter() 
    : next_msg_id_(1000), failure_rate_(0.0f), send_delay_ms_(100), ready_(true) {
}

MockMeshAdapter::~MockMeshAdapter() {
}

bool MockMeshAdapter::sendText(ChannelId channel, const std::string& text, 
                               MessageId* out_msg_id, NodeId /*peer*/) {
    if (!ready_) {
        return false;
    }
    
    // Simulate failure
    if (failure_rate_ > 0.0f) {
        float r = (float)random(0, 1000) / 1000.0f;
        if (r < failure_rate_) {
            return false;
        }
    }
    
    // Process send queue periodically
    processSendQueue();
    
    // Queue for delayed send
    PendingSend pending;
    pending.channel = channel;
    pending.text = text;
    pending.msg_id = next_msg_id_++;
    pending.queued_time = millis();
    
    send_queue_.push(pending);
    
    if (out_msg_id) {
        *out_msg_id = pending.msg_id;
    }
    
    return true;
}

bool MockMeshAdapter::pollIncomingText(MeshIncomingText* out) {
    if (receive_queue_.empty()) {
        return false;
    }
    
    *out = receive_queue_.front();
    receive_queue_.pop();
    return true;
}

void MockMeshAdapter::applyConfig(const MeshConfig& config) {
    config_ = config;
}

bool MockMeshAdapter::isReady() const {
    return ready_;
}

void MockMeshAdapter::simulateReceive(ChannelId channel, const std::string& text, NodeId from) {
    MeshIncomingText incoming;
    incoming.channel = channel;
    incoming.from = from;
    incoming.msg_id = next_msg_id_++;
    incoming.timestamp = millis() / 1000;
    incoming.text = text;
    incoming.hop_limit = 2;
    incoming.encrypted = (channel == ChannelId::SECONDARY);
    
    receive_queue_.push(incoming);
}

void MockMeshAdapter::processSendQueue() {
    uint32_t now = millis();
    
    while (!send_queue_.empty()) {
        PendingSend& pending = send_queue_.front();
        
        if (now - pending.queued_time >= send_delay_ms_) {
            // Simulate echo back (for testing)
            simulateReceive(pending.channel, pending.text, 0);
            send_queue_.pop();
        } else {
            break; // Not ready yet
        }
    }
}

} // namespace chat
