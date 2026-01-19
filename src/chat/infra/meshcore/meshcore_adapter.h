/**
 * @file meshcore_adapter.h
 * @brief MeshCore protocol adapter (placeholder/stub implementation)
 */

#pragma once

#include "../../../board/TLoRaPagerBoard.h"
#include "../../ports/i_mesh_adapter.h"
#include <queue>

namespace chat
{
namespace meshcore
{

/**
 * @brief MeshCore protocol adapter
 *
 * Placeholder implementation for MeshCore protocol support.
 * This will be implemented once the MeshCore library integration is complete.
 *
 * TODO: Implement full MeshCore protocol support
 */
class MeshCoreAdapter : public IMeshAdapter
{
  public:
    /**
     * @brief Constructor
     */
    MeshCoreAdapter(TLoRaPagerBoard& board);

    /**
     * @brief Destructor
     */
    ~MeshCoreAdapter() override = default;

    // IMeshAdapter interface implementation
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;

    bool pollIncomingText(MeshIncomingText* out) override;

    void applyConfig(const MeshConfig& config) override;

    bool isReady() const override;

    /**
     * @brief Poll for incoming raw packet data
     * @param out_data Output buffer for raw packet data
     * @param out_len Output packet length
     * @param max_len Maximum buffer size
     * @return true if raw packet data is available
     */
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;

    /**
     * @brief Handle raw packet data (from radio task)
     * @param data Raw packet data
     * @param size Packet size
     */
    void handleRawPacket(const uint8_t* data, size_t size) override;

    /**
     * @brief Process send queue (no-op placeholder)
     */
    void processSendQueue() override;

  private:
    TLoRaPagerBoard& board_;

    // Configuration
    MeshConfig config_;

    // Implementation state
    bool initialized_;

    // Receive queue for parsed messages
    std::queue<MeshIncomingText> receive_queue_;

    // Raw packet storage for debugging/inspection
    uint8_t last_raw_packet_[256];
    size_t last_raw_packet_len_;
    bool has_pending_raw_packet_;

    MessageId next_msg_id_;
};

} // namespace meshcore
} // namespace chat