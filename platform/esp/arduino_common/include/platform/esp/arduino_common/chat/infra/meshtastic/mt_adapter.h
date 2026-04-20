/**
 * @file mt_adapter.h
 * @brief Meshtastic mesh adapter
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/meshtastic/mt_codec_pb.h" // Use protobuf-based codec
#include "chat/infra/meshtastic/mt_dedup.h"
#include "chat/infra/meshtastic/mt_packet_wire.h" // Wire packet format
#include "chat/ports/i_mesh_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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
    struct MqttProxySettings
    {
        bool enabled = false;
        bool proxy_to_client_enabled = false;
        bool encryption_enabled = true;
        bool primary_uplink_enabled = false;
        bool primary_downlink_enabled = false;
        bool secondary_uplink_enabled = false;
        bool secondary_downlink_enabled = false;
        std::string root;
        std::string primary_channel_id;
        std::string secondary_channel_id;
    };

    MtAdapter(LoraBoard& board);
    virtual ~MtAdapter();
    MeshCapabilities getCapabilities() const override;

    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    bool sendTextWithId(ChannelId channel, const std::string& text,
                        MessageId forced_msg_id,
                        MessageId* out_msg_id, NodeId peer = 0) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    bool sendMeshPacket(const meshtastic_MeshPacket& packet);
    bool startKeyVerification(NodeId node_id) override;
    bool submitKeyVerificationNumber(NodeId node_id, uint64_t nonce, uint32_t number) override;
    bool isPkiReady() const override;
    bool hasPkiKey(NodeId dest) const override;
    bool getNodePublicKey(NodeId node_id, uint8_t out_key[32]) const;
    bool getOwnPublicKey(uint8_t out_key[32]) const;
    void rememberNodePublicKey(NodeId node_id, const uint8_t* key, size_t key_len);
    void forgetNodePublicKey(NodeId node_id);
    meshtastic_Routing_Error getLastRoutingError() const;
    void setMqttProxySettings(const MqttProxySettings& settings);
    bool pollMqttProxyMessage(meshtastic_MqttClientProxyMessage* out);
    bool handleMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg);
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode) override;
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
    uint32_t last_position_reply_ms_;
    std::map<uint32_t, std::string> node_long_names_;
    std::string user_long_name_;
    std::string user_short_name_;
    float last_rx_rssi_;
    float last_rx_snr_;
    uint32_t radio_freq_hz_ = 0;
    uint32_t radio_bw_hz_ = 0;
    uint8_t radio_sf_ = 0;
    uint8_t radio_cr_ = 0;

    struct PendingAckState
    {
        uint32_t dest = 0;
        ChannelId channel = ChannelId::PRIMARY;
        uint8_t channel_hash = 0;
        uint32_t last_attempt_ms = 0;
        uint8_t retransmit_count = 0;
        std::vector<uint8_t> wire_packet;
    };

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
    std::queue<meshtastic_MqttClientProxyMessage> mqtt_proxy_queue_;
    MqttProxySettings mqtt_proxy_settings_;
    std::map<uint32_t, PendingAckState> pending_ack_states_;

    static constexpr size_t MAX_PACKET_SIZE = 255;
    static constexpr uint32_t RETRY_DELAY_MS = 1000;
    static constexpr uint8_t MAX_RETRIES = 3;
    static constexpr uint32_t NODEINFO_INTERVAL_MS = 3 * 60 * 60 * 1000;
    static constexpr uint32_t NODEINFO_REPLY_SUPPRESS_MS = 12 * 60 * 60 * 1000;
    static constexpr uint32_t POSITION_REPLY_SUPPRESS_MS = 3 * 60 * 1000;
    static constexpr uint32_t PKI_BACKOFF_MS = 5 * 60 * 1000;
    static constexpr size_t MAX_APP_QUEUE = 10;
    static constexpr uint32_t ACK_TIMEOUT_MS = 15000;
    static constexpr uint8_t MAX_ACK_RETRIES = 3;
    static constexpr size_t kMaxPkiNodes = 16;
    static constexpr const char* kPkiPrefsNs = "chat_pki";
    static constexpr const char* kPkiPrefsKey = "pki_nodes";
    static constexpr const char* kPkiPrefsKeyVer = "pki_nodes_ver";
    static constexpr uint8_t kPkiPrefsVersion = 2;

    uint32_t min_tx_interval_ms_ = 0;
    uint32_t last_tx_ms_ = 0;
    uint8_t encrypt_mode_ = 1;
    meshtastic_Routing_Error last_send_error_ = meshtastic_Routing_Error_NONE;

    bool sendPacket(const PendingSend& pending);
    bool sendNodeInfo();
    bool sendNodeInfoTo(uint32_t dest, bool want_response,
                        ChannelId channel = ChannelId::PRIMARY);
    bool sendPositionTo(uint32_t dest, ChannelId channel);
    bool sendTraceRouteResponse(uint32_t dest,
                                uint32_t request_id,
                                const meshtastic_RouteDiscovery& route,
                                ChannelId channel,
                                bool want_ack);
    bool handleTraceRoutePacket(const PacketHeaderWire& header,
                                meshtastic_Data* decoded,
                                const chat::RxMeta* rx_meta,
                                ChannelId channel,
                                bool want_ack_flag,
                                bool want_response);
    void maybeBroadcastNodeInfo(uint32_t now_ms);
    void configureRadio();
    void initNodeIdentity();
    void updateChannelKeys();
    bool transmitWirePacket(const uint8_t* wire_data, size_t wire_size);
    void startRadioReceive();
    void trackPendingAck(uint32_t msg_id, uint32_t dest, ChannelId channel, uint8_t channel_hash,
                         const uint8_t* wire_data, size_t wire_size);
    void clearPendingAck(uint32_t msg_id);
    void retryPendingAck(uint32_t msg_id, PendingAckState& pending);
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
    void emitRoutingResultToPhone(uint32_t request_id,
                                  meshtastic_Routing_Error reason,
                                  uint32_t from,
                                  uint32_t to,
                                  ChannelId channel,
                                  uint8_t channel_hash,
                                  const chat::RxMeta* rx_meta);

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
    bool decodeMqttServiceEnvelope(const uint8_t* payload, size_t payload_len,
                                   meshtastic_MeshPacket* out_packet,
                                   char* out_channel_id, size_t channel_id_len,
                                   char* out_gateway_id, size_t gateway_id_len) const;
    bool injectMqttEnvelope(const meshtastic_MeshPacket& packet,
                            const char* channel_id,
                            const char* gateway_id);
    bool queueMqttProxyPublish(const meshtastic_MeshPacket& packet,
                               const char* channel_id);
    bool queueMqttProxyPublishFromWire(const uint8_t* wire_data,
                                       size_t wire_size,
                                       const meshtastic_Data* decoded,
                                       ChannelId channel_index);
    bool shouldPublishToMqtt(ChannelId channel, bool from_mqtt, bool is_pki) const;
    bool hasAnyMqttDownlinkEnabled() const;
    const char* mqttChannelIdFor(ChannelId channel) const;
    uint8_t mqttChannelHashForId(const char* channel_id, bool* out_known = nullptr,
                                 ChannelId* out_channel = nullptr) const;
    std::string mqttNodeIdString() const;
};

} // namespace meshtastic
} // namespace chat
