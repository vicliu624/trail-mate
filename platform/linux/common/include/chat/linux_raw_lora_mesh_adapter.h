#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "chat/domain/chat_model.h"
#include "chat/infra/meshtastic/mt_packet_wire.h"
#include "chat/ports/i_mesh_adapter.h"
#include "meshtastic/mesh.pb.h"
#include "platform/linux/sx126x_radio.h"

namespace trailmate::linux_app
{

class LinuxRawLoraMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    explicit LinuxRawLoraMeshAdapter(::chat::NodeId self_node_id = 0);

    [[nodiscard]] static bool hardwareCandidatePresent();

    bool begin();
    void tick();
    bool takePendingSendResult(::chat::MessageId& out_msg_id, bool& out_ok);

    ::chat::MeshCapabilities getCapabilities() const override;
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

    ::platform::linux_runtime::Sx126xRadio& radio_;
    ::chat::NodeId self_node_id_ = 0;
    ::chat::MeshConfig config_{};
    ::chat::MeshProtocol active_protocol_ = ::chat::MeshProtocol::Meshtastic;
    std::uint32_t next_msg_id_ = 0;
    std::uint8_t encrypt_mode_ = 1;
    bool duty_cycle_enabled_ = true;
    std::uint8_t channel_util_percent_ = 0;
    bool pki_ready_ = false;
    std::array<std::uint8_t, 32> pki_public_key_{};
    std::array<std::uint8_t, 32> pki_private_key_{};
    std::map<::chat::NodeId, std::array<std::uint8_t, 32>> node_public_keys_{};
    std::map<::chat::NodeId, std::uint32_t> node_key_last_seen_{};
    std::map<::chat::NodeId, std::uint32_t> nodeinfo_last_reply_ms_{};
    KeyVerificationState kv_state_ = KeyVerificationState::Idle;
    std::uint64_t kv_nonce_ = 0;
    std::uint32_t kv_nonce_ms_ = 0;
    std::uint32_t kv_security_number_ = 0;
    ::chat::NodeId kv_remote_node_ = 0;
    std::array<std::uint8_t, 32> kv_hash1_{};
    std::array<std::uint8_t, 32> kv_hash2_{};
    bool started_ = false;
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
