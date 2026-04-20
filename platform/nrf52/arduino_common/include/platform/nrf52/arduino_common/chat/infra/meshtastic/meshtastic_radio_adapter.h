#pragma once

#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/runtime/self_identity_provider.h"
#include "chat/usecase/contact_service.h"
#include "meshtastic/mesh.pb.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h"

#include <array>
#include <cstdint>
#include <map>
#include <queue>
#include <string>
#include <vector>

namespace platform::nrf52::arduino_common::chat::meshtastic
{

class MeshtasticRadioAdapter final : public ::chat::IMeshAdapter
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

    explicit MeshtasticRadioAdapter(const ::chat::runtime::SelfIdentityProvider* identity_provider = nullptr,
                                    NodeStore* node_store = nullptr,
                                    ::chat::contacts::ContactService* contact_service = nullptr);

    ::chat::MeshCapabilities getCapabilities() const override;
    bool sendText(::chat::ChannelId channel, const std::string& text,
                  ::chat::MessageId* out_msg_id, ::chat::NodeId peer = 0) override;
    bool sendTextWithId(::chat::ChannelId channel, const std::string& text,
                        ::chat::MessageId forced_msg_id,
                        ::chat::MessageId* out_msg_id, ::chat::NodeId peer = 0) override;
    bool pollIncomingText(::chat::MeshIncomingText* out) override;
    bool sendAppData(::chat::ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     ::chat::NodeId dest = 0, bool want_ack = false,
                     ::chat::MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(::chat::MeshIncomingData* out) override;
    bool requestNodeInfo(::chat::NodeId dest, bool want_response) override;
    bool startKeyVerification(::chat::NodeId node_id) override;
    bool submitKeyVerificationNumber(::chat::NodeId node_id, uint64_t nonce, uint32_t number) override;
    bool isPkiReady() const override;
    bool hasPkiKey(::chat::NodeId dest) const override;
    void applyConfig(const ::chat::MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode) override;
    bool isReady() const override;
    ::chat::NodeId getNodeId() const override;
    void setMqttProxySettings(const MqttProxySettings& settings);
    bool pollMqttProxyMessage(meshtastic_MqttClientProxyMessage* out);
    bool handleMqttProxyMessage(const meshtastic_MqttClientProxyMessage& msg);
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;
    void flushDeferredPersistence(bool force = false);

  private:
    struct PacketHistoryEntry
    {
        ::chat::NodeId sender = 0;
        ::chat::MessageId packet_id = 0;
        uint8_t next_hop = 0;
        std::array<uint8_t, 3> relayed_by{};
        uint8_t highest_hop_limit = 0;
        uint8_t our_tx_hop_limit = 0;
        uint32_t last_rx_ms = 0;
    };

    struct HistoryResult
    {
        bool seen_recently = false;
        bool was_fallback = false;
        bool we_were_next_hop = false;
        bool was_upgraded = false;
    };

    struct PendingRetransmit
    {
        std::vector<uint8_t> wire;
        ::chat::NodeId original_from = 0;
        ::chat::NodeId dest = 0;
        ::chat::MessageId packet_id = 0;
        ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY;
        uint8_t channel_hash = 0;
        uint8_t retries_left = 0;
        uint32_t next_tx_ms = 0;
        bool want_ack = false;
        bool local_origin = false;
        bool fallback_sent = false;
        bool observe_only = false;
    };

    struct TxScratchBuffers
    {
        std::array<uint8_t, 256> app_data{};
        std::array<uint8_t, 256> aux_data{};
        std::array<uint8_t, 384> wire{};
    };

    struct RxScratchBuffers
    {
        ::chat::meshtastic::PacketHeaderWire header{};
        std::array<uint8_t, 256> payload{};
        std::array<uint8_t, 256> plain{};
        meshtastic_Data decoded = meshtastic_Data_init_zero;
    };

    struct MqttScratchBuffers
    {
        std::array<uint8_t, 256> buffer{};
        std::array<uint8_t, sizeof(::chat::meshtastic::PacketHeaderWire) + 256> wire{};
        char channel_id[32] = {};
        char gateway_id[16] = {};
        meshtastic_Data decoded = meshtastic_Data_init_zero;
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        meshtastic_MqttClientProxyMessage proxy = meshtastic_MqttClientProxyMessage_init_zero;
    };

    ::chat::runtime::EffectiveSelfIdentity buildEffectiveIdentity() const;
    bool transmitWire(const uint8_t* data, size_t size);
    bool transmitPreparedWire(uint8_t* data, size_t size, ::chat::ChannelId channel,
                              const meshtastic_Data* decoded, bool track_retransmit,
                              bool local_origin, uint8_t retries_override = 0,
                              bool observe_broadcast_ack = false);
    bool buildAndQueueNodeInfo(::chat::NodeId dest, bool want_response,
                               ::chat::ChannelId channel = ::chat::ChannelId::PRIMARY);
    bool buildAndQueueRoutingPacket(::chat::NodeId dest, uint32_t request_id,
                                    uint8_t channel_hash, ::chat::ChannelId channel,
                                    meshtastic_Routing_Error reason,
                                    const uint8_t* key, size_t key_len,
                                    uint8_t hop_limit);
    bool sendRoutingAck(::chat::NodeId dest, uint32_t request_id, uint8_t channel_hash,
                        ::chat::ChannelId channel, const uint8_t* key, size_t key_len,
                        uint8_t hop_limit);
    bool sendRoutingError(::chat::NodeId dest, uint32_t request_id, uint8_t channel_hash,
                          ::chat::ChannelId channel, const uint8_t* key, size_t key_len,
                          meshtastic_Routing_Error reason, uint8_t hop_limit);
    bool sendTraceRouteResponse(::chat::NodeId dest, uint32_t request_id,
                                const meshtastic_RouteDiscovery& route,
                                ::chat::ChannelId channel, bool want_ack);
    bool sendPositionTo(::chat::NodeId dest, ::chat::ChannelId channel);
    bool handleTraceRoutePacket(const ::chat::meshtastic::PacketHeaderWire& header,
                                meshtastic_Data* decoded,
                                const ::chat::RxMeta* rx_meta,
                                ::chat::ChannelId channel,
                                bool want_ack_flag,
                                bool want_response);
    void maybeBroadcastNodeInfo(uint32_t now_ms);
    void emitRoutingResult(uint32_t request_id, meshtastic_Routing_Error reason,
                           ::chat::NodeId from, ::chat::NodeId to,
                           ::chat::ChannelId channel, uint8_t channel_hash,
                           const ::chat::RxMeta* rx_meta);
    uint8_t ourRelayId() const;
    uint8_t getLearnedNextHop(::chat::NodeId dest, uint8_t relay_node) const;
    void learnNextHop(::chat::NodeId dest, uint8_t next_hop);
    PacketHistoryEntry* findHistory(::chat::NodeId sender, ::chat::MessageId packet_id);
    const PacketHistoryEntry* findHistory(::chat::NodeId sender, ::chat::MessageId packet_id) const;
    static bool hasRelayer(const PacketHistoryEntry& entry, uint8_t relayer, bool* sole = nullptr);
    HistoryResult updatePacketHistory(const ::chat::meshtastic::PacketHeaderWire& header, bool allow_update);
    void rememberLocalPacket(const ::chat::meshtastic::PacketHeaderWire& header);
    bool maybeRebroadcast(const ::chat::meshtastic::PacketHeaderWire& header,
                          const uint8_t* payload, size_t payload_size,
                          ::chat::ChannelId channel, const meshtastic_Data* decoded);
    void updateNodeLastSeen(::chat::NodeId node_id, uint8_t hops_away, ::chat::ChannelId channel);
    void handleRoutingPacket(const ::chat::meshtastic::PacketHeaderWire& header,
                             const meshtastic_Data& decoded,
                             ::chat::ChannelId channel,
                             const uint8_t* key, size_t key_len,
                             const ::chat::RxMeta& rx_meta);
    void queuePendingRetransmit(const ::chat::meshtastic::PacketHeaderWire& header,
                                const uint8_t* wire, size_t wire_size,
                                ::chat::ChannelId channel,
                                bool local_origin, uint8_t retries_override,
                                bool observe_only = false);
    bool stopPendingRetransmit(::chat::NodeId from, ::chat::MessageId packet_id);
    void maybeHandleObservedRelay(const ::chat::meshtastic::PacketHeaderWire& header);
    static uint64_t pendingKey(::chat::NodeId from, ::chat::MessageId packet_id);
    bool initPkiKeys();
    void loadPkiNodeKeys();
    void savePkiNodeKey(::chat::NodeId node_id, const uint8_t* key, size_t key_len);
    void markPkiKeysDirty();
    bool savePkiKeysToPrefs();
    void touchPkiNodeKey(::chat::NodeId node_id);
    bool decryptPkiPayload(::chat::NodeId from, ::chat::MessageId packet_id,
                           const uint8_t* cipher, size_t cipher_len,
                           uint8_t* out_plain, size_t* out_plain_len);
    bool encryptPkiPayload(::chat::NodeId dest, ::chat::MessageId packet_id,
                           const uint8_t* plain, size_t plain_len,
                           uint8_t* out_cipher, size_t* out_cipher_len);
    void updateKeyVerificationState();
    void resetKeyVerificationState();
    void buildVerificationCode(char* out, size_t out_len) const;
    bool handleKeyVerificationInit(const ::chat::meshtastic::PacketHeaderWire& header,
                                   const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationReply(const ::chat::meshtastic::PacketHeaderWire& header,
                                    const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationFinal(const ::chat::meshtastic::PacketHeaderWire& header,
                                    const meshtastic_KeyVerification& kv);
    bool sendKeyVerificationPacket(::chat::NodeId dest, const meshtastic_KeyVerification& kv,
                                   bool want_response);
    bool processKeyVerificationNumber(::chat::NodeId remote_node, uint64_t nonce, uint32_t number);
    std::string mqttNodeIdString() const;
    const char* mqttChannelIdFor(::chat::ChannelId channel) const;
    bool hasAnyMqttDownlinkEnabled() const;
    bool shouldPublishToMqtt(::chat::ChannelId channel, bool from_mqtt, bool is_pki) const;
    uint8_t mqttChannelHashForId(const char* channel_id, bool* out_known = nullptr,
                                 ::chat::ChannelId* out_channel = nullptr) const;
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
                                       ::chat::ChannelId channel_index);

    ::chat::MeshConfig config_{};
    ::chat::NodeId node_id_ = 0;
    ::chat::MessageId next_packet_id_ = 1;
    std::string long_name_;
    std::string short_name_;
    const ::chat::runtime::SelfIdentityProvider* identity_provider_ = nullptr;
    NodeStore* node_store_ = nullptr;
    ::chat::contacts::ContactService* contact_service_ = nullptr;
    float last_rx_rssi_ = 0.0f;
    float last_rx_snr_ = 0.0f;
    std::queue<::chat::MeshIncomingText> text_queue_;
    std::queue<::chat::MeshIncomingData> data_queue_;
    std::queue<meshtastic_MqttClientProxyMessage> mqtt_proxy_queue_;
    MqttProxySettings mqtt_proxy_settings_{};
    std::vector<PacketHistoryEntry> packet_history_;
    std::map<uint64_t, PendingRetransmit> pending_retransmits_;
    uint32_t last_nodeinfo_ms_ = 0;
    uint8_t last_raw_packet_[256] = {};
    size_t last_raw_packet_len_ = 0;
    bool has_pending_raw_packet_ = false;
    uint8_t encrypt_mode_ = 1;
    bool pki_ready_ = false;
    std::array<uint8_t, 32> pki_public_key_{};
    std::array<uint8_t, 32> pki_private_key_{};
    std::map<::chat::NodeId, std::array<uint8_t, 32>> node_public_keys_;
    std::map<::chat::NodeId, uint32_t> node_key_last_seen_;
    uint32_t pki_node_keys_save_due_ms_ = 0;
    bool pki_node_keys_dirty_ = false;
    std::map<::chat::NodeId, ::chat::ChannelId> node_last_channel_;
    std::map<::chat::NodeId, uint32_t> nodeinfo_last_seen_ms_;
    uint32_t last_position_reply_ms_ = 0;
    TxScratchBuffers tx_scratch_{};
    RxScratchBuffers rx_scratch_{};
    MqttScratchBuffers mqtt_scratch_{};

    enum class KeyVerificationState : uint8_t
    {
        Idle,
        SenderInitiated,
        SenderAwaitingNumber,
        SenderAwaitingUser,
        ReceiverAwaitingHash1,
        ReceiverAwaitingUser
    };

    KeyVerificationState kv_state_ = KeyVerificationState::Idle;
    uint64_t kv_nonce_ = 0;
    uint32_t kv_nonce_ms_ = 0;
    uint32_t kv_security_number_ = 0;
    ::chat::NodeId kv_remote_node_ = 0;
    std::array<uint8_t, 32> kv_hash1_{};
    std::array<uint8_t, 32> kv_hash2_{};
};

} // namespace platform::nrf52::arduino_common::chat::meshtastic
