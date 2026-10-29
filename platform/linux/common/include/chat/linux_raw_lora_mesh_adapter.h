#pragma once
#include "mesh/domain/peer_identity.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "chat/delivery/chat_delivery_types.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/infra/rnode/rnode_packet_wire.h"
#include "chat/infra/voice/vmp_control_ingress.h"
#include "chat/linux_meshcore_direct_routes.h"
#include "chat/linux_meshcore_direct_state.h"
#include "chat/linux_meshcore_discovery.h"
#include "chat/linux_meshcore_repeater.h"
#include "chat/linux_meshcore_verification.h"
#include "chat/ports/i_mesh_adapter.h"
#include "meshtastic/mesh.pb.h"
#include "platform/linux/identity_owner_lease.h"
#include "platform/linux/mesh/linux_meshcore_identity.h"
#include "platform/linux/packet_radio.h"
#include "platform/linux/retry_backoff.h"

namespace trailmate::linux_app
{
class LinuxMeshMqttBridge;

class LinuxRawLoraMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    explicit LinuxRawLoraMeshAdapter(::chat::NodeId self_node_id = 0);
    LinuxRawLoraMeshAdapter(::platform::linux_runtime::IPacketRadio& radio, ::chat::NodeId self_node_id);
    using Clock = uint32_t (*)();
    LinuxRawLoraMeshAdapter(::platform::linux_runtime::IPacketRadio& radio, ::chat::NodeId self_node_id, Clock retry_clock);
    ~LinuxRawLoraMeshAdapter() override;

    [[nodiscard]] static bool hardwareCandidatePresent();

    bool begin();
    void tick();
    bool takePendingSendResult(::chat::MessageId& out_msg_id, bool& out_ok);
    using SendResultSink = void (*)(void*, ::chat::MessageId, ::chat::MeshProtocol,
                                    ::chat::MessageStatus, ::chat::delivery::SendFailureKind);
    void setSendResultSink(SendResultSink sink, void* context);
    using MessageIdSource = uint32_t (*)(void*);
    void setMessageIdSource(MessageIdSource source, void* context);
    // Adapter-owner thread only. The sink must outlive its registration and
    // copy into bounded storage without doing radio or UI work in enqueue.
    void setVoiceControlSink(::chat::voice::vmp::IControlEnvelopeSink* sink);
    struct ReticulumPacket
    {
        std::array<uint8_t, ::chat::rnode::kRNodeMaxPayloadSize> data{};
        size_t size = 0;
        float rssi_dbm = 0, snr_db = 0;
    };
    using ReticulumPacketSink = void (*)(void*, const ReticulumPacket&);
    void setReticulumPacketSink(ReticulumPacketSink, void*);
    bool sendReticulumControl(const uint8_t*, size_t);
    std::vector<::chat::NodeId> encryptedPeerIds() const;
    std::vector<::mesh::PeerPublicKey> peerPublicKeys() const;
    void cancelPendingSends();
    void suspendRadio();
    void setMqttBridge(LinuxMeshMqttBridge*);
    void setMqttVoiceSink(std::function<bool(const uint8_t*, size_t)> sink) { mqtt_voice_sink_ = std::move(sink); }
    void resetTransportContext();
    bool mqttActive() const;
    bool sendMeshCoreNodeInfo(::chat::NodeId, bool query, ::chat::ChannelId);
    bool consumeMeshCoreNodeInfo(const ::chat::MeshIncomingData&);

    ::chat::MeshCapabilities getCapabilities() const override;
    ::chat::MeshProtocol backendProtocol() const override { return active_protocol_; }
    bool sendText(::chat::ChannelId channel,
                  const std::string& text,
                  ::chat::MessageId* out_msg_id,
                  ::chat::NodeId peer = 0) override;
    bool sendTextWithId(::chat::ChannelId channel,
                        const std::string& text,
                        ::chat::MessageId forced_msg_id,
                        ::chat::MessageId* out_msg_id,
                        ::chat::NodeId peer = 0) override;
    bool pollIncomingText(::chat::MeshIncomingText* out) override;
    ::chat::MeshSendResult sendTextDetailed(::chat::ChannelId channel, const std::string& text,
                                            ::chat::MessageId forced_msg_id = 0, ::chat::NodeId peer = 0) override;
    bool sendAppData(::chat::ChannelId channel,
                     std::uint32_t portnum,
                     const std::uint8_t* payload,
                     std::size_t len,
                     ::chat::NodeId dest = 0,
                     bool want_ack = false,
                     ::chat::MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(::chat::MeshIncomingData* out) override;
    bool requestNodeInfo(::chat::NodeId dest, bool want_response) override;
    bool triggerDiscoveryAction(::chat::MeshDiscoveryAction action) override;
    ::chat::MeshActionResult triggerDiscoveryActionDetailed(
        ::chat::MeshDiscoveryAction action) override;
    bool startKeyVerification(::chat::NodeId node_id) override;
    bool submitKeyVerificationNumber(::chat::NodeId node_id,
                                     std::uint64_t nonce,
                                     std::uint32_t number) override;
    void applyConfig(const ::chat::MeshConfig& config) override;
    void applyProtocolConfig(::chat::MeshProtocol protocol,
                             const ::chat::MeshConfig& config);
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool isPkiReady() const override;
    bool hasPkiKey(::chat::NodeId dest) const override;
    void setNetworkLimits(bool duty_cycle_enabled,
                          std::uint8_t util_percent) override;
    void setPrivacyConfig(std::uint8_t encrypt_mode) override;
    ::chat::NodeId getNodeId() const override;
    bool isReady() const override;
    bool pollIncomingRawPacket(std::uint8_t* out_data,
                               std::size_t& out_len,
                               std::size_t max_len) override;
    void processSendQueue() override;

    void setSelfNodeId(::chat::NodeId id);
    [[nodiscard]] std::string statusText() const;
    [[nodiscard]] std::string radioConfigText() const;
    [[nodiscard]] std::string radioStatsText() const;
    [[nodiscard]] std::vector<std::string> diagnosticLines() const;

  private:
    struct MqttScratch;
    LinuxMeshMqttBridge* mqtt_ = nullptr;
    std::function<bool(const uint8_t*, size_t)> mqtt_voice_sink_;
    std::unique_ptr<MqttScratch> mqtt_scratch_;
    void consumeMqtt();
    bool consumeMeshtasticMqtt(const meshtastic_MeshPacket&, ::chat::ChannelId, bool pki);
    bool consumeMeshtasticData(const ::chat::meshtastic::PacketHeaderWire&, const meshtastic_Data&,
                               const ::platform::linux_runtime::RadioPacket&, ::chat::ChannelId,
                               const ::chat::RxMeta&, bool encrypted, const uint8_t*, size_t);
    bool transmitMeshtasticFrame(const uint8_t*, size_t);
    void networkMetadata(::chat::RxMeta&) const;
    enum class PacketKind : std::uint8_t
    {
        Text = 1,
        AppData = 2,
    };

    struct PendingResult
    {
        ::chat::MessageId msg_id = 0;
        bool ok = false;
    };

    enum class KeyVerificationState : std::uint8_t
    {
        Idle,
        SenderInitiated,
        SenderAwaitingNumber,
        SenderAwaitingUser,
        ReceiverAwaitingHash1,
        ReceiverAwaitingUser,
    };

    bool ensureRadioReady();
    bool meshCoreVerificationNumber(::chat::NodeId peer, uint64_t nonce, uint32_t& number) const;
    bool publishMeshCoreVerification(const meshcore_wire::Verification::Action&);
    void updateMeshCoreVerification();
    void cancelMeshCoreVerification();
    void receiveMeshCoreVerification(::chat::NodeId peer, const uint8_t*, size_t);
    ::chat::MessageId nextMessageId();
    void logStatusIfChanged(const char* title, const std::string& status);
    void logRadioStatsChanges();
    void logRxMonitorHeartbeat();
    bool sendMeshtasticPayload(::chat::ChannelId channel,
                               ::chat::NodeId dest,
                               ::chat::MessageId msg_id,
                               std::uint32_t portnum,
                               const std::uint8_t* payload,
                               std::size_t len,
                               bool want_ack,
                               bool want_response);
    bool sendMeshtasticNodeInfoTo(::chat::NodeId dest,
                                  bool want_response,
                                  ::chat::ChannelId channel);
    bool sendKeyVerificationPacket(::chat::NodeId dest,
                                   const meshtastic_KeyVerification& kv,
                                   bool want_response);
    bool sendRoutingAck(::chat::NodeId dest,
                        ::chat::MessageId request_id,
                        std::uint8_t channel_hash,
                        const std::uint8_t* psk,
                        std::size_t psk_len);
    bool encryptPkiPayload(::chat::NodeId dest,
                           ::chat::MessageId packet_id,
                           const std::uint8_t* plain,
                           std::size_t plain_len,
                           std::uint8_t* out_cipher,
                           std::size_t* out_cipher_len);
    bool decryptPkiPayload(::chat::NodeId from,
                           ::chat::MessageId packet_id,
                           const std::uint8_t* cipher,
                           std::size_t cipher_len,
                           std::uint8_t* out_plain,
                           std::size_t* out_plain_len);
    bool initPkiKeys();
    void loadPkiNodeKeys();
    void savePkiNodeKey(::chat::NodeId node_id,
                        const std::uint8_t* key,
                        std::size_t key_len);
    void savePkiKeysToStore();
    void forgetPkiNodeKey(::chat::NodeId node_id);
    void touchPkiNodeKey(::chat::NodeId node_id);
    void updateKeyVerificationState();
    void resetKeyVerificationState();
    void buildVerificationCode(char* out, std::size_t out_len) const;
    bool handleKeyVerificationInit(
        const ::chat::meshtastic::PacketHeaderWire& header,
        const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationReply(
        const ::chat::meshtastic::PacketHeaderWire& header,
        const meshtastic_KeyVerification& kv);
    bool handleKeyVerificationFinal(
        const ::chat::meshtastic::PacketHeaderWire& header,
        const meshtastic_KeyVerification& kv);
    bool processKeyVerificationNumber(::chat::NodeId remote_node,
                                      std::uint64_t nonce,
                                      std::uint32_t number);
    bool sendFrame(PacketKind kind,
                   ::chat::ChannelId channel,
                   ::chat::NodeId dest,
                   ::chat::MessageId msg_id,
                   std::uint32_t portnum,
                   const std::uint8_t* payload,
                   std::size_t len);
    bool parseFrame(const ::platform::linux_runtime::Sx126xPacket& packet);
    bool parseMeshtasticPacket(
        const ::platform::linux_runtime::Sx126xPacket& packet);
    bool sendMeshCoreGroup(::chat::ChannelId channel, ::chat::NodeId dest,
                           std::uint32_t portnum, const std::uint8_t* payload,
                           std::size_t len, bool text);
    bool parseMeshCoreGroup(const ::platform::linux_runtime::RadioPacket& packet);
    bool rememberMeshCoreSignature(uint32_t signature);
    bool sendMeshCoreAdvert(bool broadcast);
    bool parseMeshCoreAdvert(const ::platform::linux_runtime::RadioPacket& packet);
    ::chat::MeshActionResult startMeshCoreDiscovery();
    void parseMeshCoreControl(const ::platform::linux_runtime::RadioPacket& packet);
    meshcore_wire::Discovery meshcore_discovery_;
    meshcore_wire::Repeater meshcore_repeater_;
    bool transmitMeshCoreFrame(const uint8_t* bytes, size_t size);
    meshcore_wire::DirectState meshcore_direct_;
    meshcore_wire::DirectRoutes meshcore_direct_routes_;
    ::chat::MeshOperationFailure meshcore_send_failure_ = ::chat::MeshOperationFailure::Unknown;
    bool sendMeshCoreDirect(::chat::NodeId peer, ::chat::MessageId message, uint32_t port,
                            const uint8_t* bytes, std::size_t size, bool want_ack, bool text);
    bool parseMeshCoreDirect(const ::platform::linux_runtime::RadioPacket& packet);
    void publishMeshCoreDirectOutcome(const meshcore_wire::DirectOutcome& outcome,
                                      ::chat::MessageStatus status, bool timeout);
    ::platform::linux_runtime::LinuxMeshCoreIdentity meshcore_identity_;
    ReticulumPacketSink reticulum_sink_ = nullptr;
    void* reticulum_context_ = nullptr;
    ::chat::rnode::ReassemblyState reticulum_reassembly_{};
    ::chat::rnode::EncodedAirPacketSet reticulum_tx_{};
    ReticulumPacket reticulum_rx_{};
    uint8_t reticulum_sequence_ = 0;
    std::map<::chat::NodeId, std::array<uint8_t, 32>> meshcore_peer_keys_;
    std::map<::chat::NodeId, uint32_t> meshcore_peer_epochs_;
    bool meshcore_peers_loaded_ = false;
    struct SeenMeshCorePacket
    {
        uint32_t signature = 0;
        uint32_t timestamp = 0;
        bool valid = false;
    };
    std::array<SeenMeshCorePacket, 64> meshcore_seen_{};
    std::size_t meshcore_seen_cursor_ = 0;

    ::platform::linux_runtime::IPacketRadio& radio_;
    ::chat::voice::vmp::ControlIngress voice_control_ingress_;
    bool radio_acquired_ = false;
    SendResultSink send_result_sink_ = nullptr;
    void* send_result_context_ = nullptr;
    MessageIdSource message_id_source_ = nullptr;
    void* message_id_context_ = nullptr;
    void publishSendResult(::chat::MessageId id, ::chat::MeshProtocol protocol,
                           ::chat::MessageStatus status, ::chat::delivery::SendFailureKind failure);
    ::platform::linux_runtime::RetryBackoff radio_retry_;
    Clock retry_clock_;
    ::chat::NodeId self_node_id_ = 0;
    ::chat::MeshConfig config_{};
    ::chat::MeshProtocol active_protocol_ = ::chat::MeshProtocol::Meshtastic;
    std::uint32_t next_msg_id_ = 0;
    std::uint8_t encrypt_mode_ = 1;
    bool duty_cycle_enabled_ = true;
    std::uint8_t channel_util_percent_ = 0;
    bool pki_ready_ = false;
    ::platform::linux_runtime::IdentityOwnerLease pki_owner_;
    std::array<std::uint8_t, 32> pki_public_key_{};
    std::array<std::uint8_t, 32> pki_private_key_{};
    std::map<::chat::NodeId, std::array<std::uint8_t, 32>> node_public_keys_{};
    std::map<::chat::NodeId, std::uint32_t> node_key_last_seen_{};
    std::map<::chat::NodeId, std::uint32_t> nodeinfo_last_reply_ms_{};
    KeyVerificationState kv_state_ = KeyVerificationState::Idle;
    meshcore_wire::Verification meshcore_verification_;
    std::uint64_t kv_nonce_ = 0;
    std::uint32_t kv_nonce_ms_ = 0;
    std::uint32_t kv_security_number_ = 0;
    ::chat::NodeId kv_remote_node_ = 0;
    std::array<std::uint8_t, 32> kv_hash1_{};
    std::array<std::uint8_t, 32> kv_hash2_{};
    bool started_ = false;
    bool pki_initialized_ = false;
    bool tx_enabled_ = true;
    std::string long_name_{};
    std::string short_name_{};
    std::string last_status_ = "LoRa driver not started.";
    std::deque<::chat::MeshIncomingText> incoming_text_{};
    std::deque<::chat::MeshIncomingData> incoming_data_{};
    std::deque<PendingResult> pending_results_{};
    std::deque<::platform::linux_runtime::Sx126xPacket> raw_packets_{};
    ::platform::linux_runtime::Sx126xRadioStats last_logged_stats_{};
    bool stats_logged_ = false;
    std::string last_logged_status_{};
    std::uint32_t last_rx_monitor_log_s_ = 0;
};

} // namespace trailmate::linux_app
