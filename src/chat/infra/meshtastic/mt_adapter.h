/**
 * @file mt_adapter.h
 * @brief Meshtastic mesh adapter
 */

#pragma once

#include "../../../board/LoraBoard.h"
#include "../../domain/chat_types.h"
#include "../../ports/i_mesh_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "mt_codec_pb.h" // Use protobuf-based codec
#include "mt_dedup.h"
#include "mt_packet_wire.h" // Wire packet format
#include <array>
#include <map>
#include <queue>
#include <string>

namespace chat
{
namespace meshtastic
{

/**
 * @brief Meshtastic mesh adapter
 * Implements IMeshAdapter using Meshtastic protocol over LoRa
 */
class MtAdapter : public chat::IMeshAdapter
{
  public:
    MtAdapter(LoraBoard& board);
    virtual ~MtAdapter();

    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    bool startKeyVerification(NodeId node_id) override;
    bool submitKeyVerificationNumber(NodeId node_id, uint64_t nonce, uint32_t number) override;
    bool isPkiReady() const override;
    bool hasPkiKey(NodeId dest) const override;
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
     * @brief Process received packets (call from radio task)
     */
    void processReceivedPacket(const uint8_t* data, size_t size);

    /**
     * @brief Process send queue (call periodically)
     */
    void processSendQueue() override;

  private:
    LoraBoard& board_;
    MeshConfig config_;
    MtDedup dedup_;
    MessageId next_packet_id_;
    bool ready_;
    NodeId node_id_;
    uint8_t mac_addr_[6];
    uint32_t last_nodeinfo_ms_;
    uint8_t primary_channel_hash_;
    uint8_t primary_psk_[16];
    size_t primary_psk_len_;
    uint8_t secondary_channel_hash_;
    uint8_t secondary_psk_[16];
    size_t secondary_psk_len_;
    bool pki_ready_;
    std::array<uint8_t, 32> pki_public_key_;
    std::array<uint8_t, 32> pki_private_key_;
    std::map<uint32_t, std::array<uint8_t, 32>> node_public_keys_;
    std::map<uint32_t, uint32_t> node_key_last_seen_;
    std::map<uint32_t, ChannelId> node_last_channel_;
    std::map<uint32_t, uint32_t> nodeinfo_last_seen_ms_;
    std::map<uint32_t, uint32_t> pending_ack_ms_;
    std::map<uint32_t, uint32_t> pending_ack_dest_;
    std::map<uint32_t, std::string> node_long_names_;
    std::string user_long_name_;
    std::string user_short_name_;
    float last_rx_rssi_;
    float last_rx_snr_;

    enum class KeyVerificationState : uint8_t
    {
        Idle,
        SenderInitiated,
        SenderAwaitingNumber,
        SenderAwaitingUser,
        ReceiverAwaitingHash1,
        ReceiverAwaitingUser
    };

    KeyVerificationState kv_state_;
    uint64_t kv_nonce_;
    uint32_t kv_nonce_ms_;
    uint32_t kv_security_number_;
    uint32_t kv_remote_node_;
    std::array<uint8_t, 32> kv_hash1_;
    std::array<uint8_t, 32> kv_hash2_;

    // Raw packet data storage for protocol detection
    uint8_t last_raw_packet_[256];
    size_t last_raw_packet_len_;
    bool has_pending_raw_packet_;

    struct PendingSend
    {
        ChannelId channel;
        uint32_t portnum;
        std::string text;
        MessageId msg_id;
        NodeId dest;
        uint32_t retry_count;
        uint32_t last_attempt;
    };

    std::queue<PendingSend> send_queue_;
    std::queue<MeshIncomingText> receive_queue_;
    std::queue<MeshIncomingData> app_receive_queue_;

    static constexpr size_t MAX_PACKET_SIZE = 255;
    static constexpr uint32_t RETRY_DELAY_MS = 1000;
    static constexpr uint8_t MAX_RETRIES = 1;
    static constexpr uint32_t NODEINFO_INTERVAL_MS = 3 * 60 * 60 * 1000;
    static constexpr uint32_t NODEINFO_REPLY_SUPPRESS_MS = 12 * 60 * 60 * 1000;
    static constexpr uint32_t PKI_BACKOFF_MS = 5 * 60 * 1000;
    static constexpr size_t MAX_APP_QUEUE = 10;
    static constexpr uint32_t ACK_TIMEOUT_MS = 15000;
    static constexpr size_t kMaxPkiNodes = 16;
    static constexpr const char* kPkiPrefsNs = "chat_pki";
    static constexpr const char* kPkiPrefsKey = "pki_nodes";
    static constexpr const char* kPkiPrefsKeyVer = "pki_nodes_ver";
    static constexpr uint8_t kPkiPrefsVersion = 2;

    uint32_t min_tx_interval_ms_ = 0;
    uint32_t last_tx_ms_ = 0;
    uint8_t encrypt_mode_ = 1;
    bool pki_enabled_ = false;

    bool sendPacket(const PendingSend& pending);
    bool sendNodeInfo();
    bool sendNodeInfoTo(uint32_t dest, bool want_response);
    void maybeBroadcastNodeInfo(uint32_t now_ms);
    void configureRadio();
    void initNodeIdentity();
    void updateChannelKeys();
    void startRadioReceive();
    bool initPkiKeys();
    void loadPkiNodeKeys();
    void savePkiNodeKey(uint32_t node_id, const uint8_t* key, size_t key_len);
    bool decryptPkiPayload(uint32_t from, uint32_t packet_id,
                           const uint8_t* cipher, size_t cipher_len,
                           uint8_t* out_plain, size_t* out_plain_len);
    bool encryptPkiPayload(uint32_t dest, uint32_t packet_id,
                           const uint8_t* plain, size_t plain_len,
                           uint8_t* out_cipher, size_t* out_cipher_len);
    void savePkiKeysToPrefs();
    void touchPkiNodeKey(uint32_t node_id);
    bool sendRoutingAck(uint32_t dest, uint32_t request_id, uint8_t channel_hash,
                        const uint8_t* psk, size_t psk_len);
    bool sendRoutingError(uint32_t dest, uint32_t request_id, uint8_t channel_hash,
                          const uint8_t* psk, size_t psk_len,
                          meshtastic_Routing_Error reason);

    void updateKeyVerificationState();
    void resetKeyVerificationState();
    bool handleKeyVerificationInit(const PacketHeaderWire& header,
                                   const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationReply(const PacketHeaderWire& header,
                                    const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationFinal(const PacketHeaderWire& header,
                                    const meshtastic_KeyVerification& kv);
    bool sendKeyVerificationPacket(uint32_t dest, const meshtastic_KeyVerification& kv,
                                   bool want_response);
    bool processKeyVerificationNumber(uint32_t remote_node, uint64_t nonce, uint32_t number);
    void buildVerificationCode(char* out, size_t out_len) const;
};

} // namespace meshtastic
} // namespace chat
