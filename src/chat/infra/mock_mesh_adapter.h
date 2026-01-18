/**
 * @file mock_mesh_adapter.h
 * @brief Mock mesh adapter for testing
 */

#pragma once

#include "../ports/i_mesh_adapter.h"
#include "../domain/chat_types.h"
#include <queue>
#include <string>

namespace chat {

/**
 * @brief Mock mesh adapter for UI testing
 * Simulates mesh behavior without actual LoRa communication
 */
class MockMeshAdapter : public IMeshAdapter {
public:
    MockMeshAdapter();
    virtual ~MockMeshAdapter();
    
    bool sendText(ChannelId channel, const std::string& text, 
                 MessageId* out_msg_id, NodeId peer = 0) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    void applyConfig(const MeshConfig& config) override;
    bool isReady() const override;
    
    /**
     * @brief Simulate receiving a message (for testing)
     */
    void simulateReceive(ChannelId channel, const std::string& text, NodeId from = 12345);
    
    /**
     * @brief Set failure rate (0.0 = never fail, 1.0 = always fail)
     */
    void setFailureRate(float rate) {
        failure_rate_ = rate;
    }
    
    /**
     * @brief Set delay before send succeeds (ms)
     */
    void setSendDelay(uint32_t delay_ms) {
        send_delay_ms_ = delay_ms;
    }

private:
    struct PendingSend {
        ChannelId channel;
        std::string text;
        MessageId msg_id;
        uint32_t queued_time;
    };
    
    std::queue<PendingSend> send_queue_;
    std::queue<MeshIncomingText> receive_queue_;
    MessageId next_msg_id_;
    float failure_rate_;
    uint32_t send_delay_ms_;
    bool ready_;
    MeshConfig config_;
    
    void processSendQueue();
};

} // namespace chat
