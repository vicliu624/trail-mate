/**
 * @file meshcore_adapter.h
 * @brief MeshCore protocol adapter
 */

#pragma once

#include "../../../board/LoraBoard.h"
#include "../../ports/i_mesh_adapter.h"
#include <deque>
#include <queue>
#include <string>
#include <vector>

namespace chat
{
namespace meshcore
{

/**
 * @brief MeshCore protocol adapter
 */
class MeshCoreAdapter : public IMeshAdapter
{
  public:
    /**
     * @brief Constructor
     */
    MeshCoreAdapter(LoraBoard& board);

    /**
     * @brief Destructor
     */
    ~MeshCoreAdapter() override = default;

    // IMeshAdapter interface implementation
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;

    bool pollIncomingText(MeshIncomingText* out) override;

    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false) override;

    bool pollIncomingData(MeshIncomingData* out) override;

    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode, bool pki_enabled) override;
    void setLastRxStats(float rssi, float snr) override;

    bool isReady() const override;
    NodeId getNodeId() const override { return node_id_; }

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
    static constexpr size_t kMaxPeerPathLen = 64;

    struct ScheduledFrame
    {
        std::vector<uint8_t> bytes;
        uint32_t due_ms = 0;
    };

    struct SeenEntry
    {
        uint32_t signature = 0;
        uint32_t seen_ms = 0;
    };

    struct PeerRouteEntry
    {
        uint8_t peer_hash = 0;
        NodeId node_id_guess = 0;
        uint8_t out_path[kMaxPeerPathLen] = {};
        uint8_t out_path_len = 0;
        bool has_out_path = false;
        ChannelId preferred_channel = ChannelId::PRIMARY;
        uint32_t last_seen_ms = 0;
    };

    LoraBoard& board_;

    // Configuration
    MeshConfig config_;
    std::string user_long_name_;
    std::string user_short_name_;
    uint32_t min_tx_interval_ms_;
    uint32_t last_tx_ms_;
    uint8_t encrypt_mode_;
    bool pki_enabled_;
    NodeId node_id_;
    uint8_t self_hash_;
    float last_rx_rssi_;
    float last_rx_snr_;

    // Implementation state
    bool initialized_;

    // Receive queue for parsed messages
    std::queue<MeshIncomingText> receive_queue_;
    std::queue<MeshIncomingData> app_receive_queue_;

    // Raw packet storage for debugging/inspection
    uint8_t last_raw_packet_[256];
    size_t last_raw_packet_len_;
    bool has_pending_raw_packet_;
    std::deque<ScheduledFrame> scheduled_tx_;
    std::deque<SeenEntry> seen_recent_;
    std::vector<PeerRouteEntry> peer_routes_;

    MessageId next_msg_id_;

    bool canTransmitNow(uint32_t now_ms) const;
    bool transmitFrameNow(const uint8_t* data, size_t len, uint32_t now_ms);
    bool enqueueScheduled(const uint8_t* data, size_t len, uint32_t delay_ms);
    bool resolveGroupSecret(ChannelId channel, uint8_t out_key16[16],
                            uint8_t out_key32[32], uint8_t* out_hash) const;
    ChannelId resolveChannelFromHash(uint8_t channel_hash, bool* out_match) const;
    bool deriveDirectSecret(ChannelId channel, uint8_t peer_hash,
                            uint8_t out_key16[16], uint8_t out_key32[32]) const;
    bool tryDecryptPeerPayload(uint8_t src_hash, const uint8_t* cipher, size_t cipher_len,
                               uint8_t* out_plain, size_t* out_plain_len,
                               ChannelId* out_channel) const;
    PeerRouteEntry* findPeerRouteByHash(uint8_t peer_hash);
    const PeerRouteEntry* findPeerRouteByHash(uint8_t peer_hash) const;
    PeerRouteEntry& upsertPeerRoute(uint8_t peer_hash, uint32_t now_ms);
    void rememberPeerNodeId(uint8_t peer_hash, NodeId node_id, uint32_t now_ms);
    NodeId resolvePeerNodeId(uint8_t peer_hash) const;
    void rememberPeerPath(uint8_t peer_hash, const uint8_t* path, size_t path_len,
                          ChannelId channel, uint32_t now_ms);
    void pruneSeen(uint32_t now_ms);
    bool hasSeenSignature(uint32_t signature, uint32_t now_ms);
};

} // namespace meshcore
} // namespace chat
