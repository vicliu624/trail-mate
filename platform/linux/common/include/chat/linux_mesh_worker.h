#pragma once
#include "chat/delivery/chat_delivery_types.h"
#include "chat/domain/reticulum_network_config.h"
#include "chat/ports/i_incoming_delivery_commit_port.h"
#include "chat/ports/i_mesh_adapter.h"
#include "mesh/domain/peer_identity.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace platform::linux_runtime
{
class IPacketRadio;
class IRadioPowerControl;
class RadioPowerRequests;
} // namespace platform::linux_runtime
namespace trailmate::linux_app
{
class ILinuxVoiceControlSink;
// UI-facing mesh port. Hardware/protocol/storage work belongs to one worker.
// initialize() may wait for identity bootstrap and is intended for app startup.
// Other methods enqueue bounded work or read cached state; shutdown() joins.
class LinuxMeshWorker final : public chat::IMeshAdapter, public chat::IIncomingDeliveryCommitPort
{
  public:
    struct Outcome
    {
        chat::MessageId message = 0;
        chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
        chat::MessageStatus status = chat::MessageStatus::Failed;
        chat::delivery::SendFailureKind failure = chat::delivery::SendFailureKind::Unknown;
    };
    struct PeerKeys
    {
        chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
        std::vector<::mesh::PeerPublicKey> keys;
    };
    PeerKeys peerPublicKeys() const;
    explicit LinuxMeshWorker(chat::NodeId base_id);
    LinuxMeshWorker(chat::NodeId base_id, platform::linux_runtime::IPacketRadio* radio,
                    platform::linux_runtime::IRadioPowerControl* power = nullptr);
    LinuxMeshWorker(chat::NodeId base_id, platform::linux_runtime::IPacketRadio*,
                    platform::linux_runtime::IRadioPowerControl*, bool allow_physical_radio);
    LinuxMeshWorker(platform::linux_runtime::IPacketRadio& radio, chat::NodeId base_id);
    ~LinuxMeshWorker() override;
    bool initialize();
    void shutdown();
    // The sink must outlive shutdown(); callbacks only run on the mesh worker.
    void setVoiceControlSink(ILinuxVoiceControlSink*);
    // Voice-worker only: waits for a complete bounded MQTT socket write.
    bool publishVoiceMqtt(const uint8_t*, size_t, const std::atomic_bool& cancel);
    std::shared_ptr<platform::linux_runtime::RadioPowerRequests> powerRequests() const;
    void tick();
    bool takeOutcome(Outcome& out);
    void setSelfNodeId(chat::NodeId id);
    void applyProtocolConfig(chat::MeshProtocol protocol, const chat::MeshConfig& config);
    struct MqttSettings
    {
        bool enabled = false, uplink = true, downlink = true;
        std::string host, root, username, password;
        uint16_t port = 1883;
    };
    void applyProtocolConfig(chat::MeshProtocol, const chat::MeshConfig&, const MqttSettings&);
    void applyReticulumNetworkConfig(const chat::reticulum::ReticulumNetworkConfig&);
    void clearReticulumNetworkOverride();
    bool reticulumNetworkConfigPending() const;
    void applyConfig(const chat::MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool enabled, uint8_t percent) override;
    void setPrivacyConfig(uint8_t mode) override;
    chat::MeshCapabilities getCapabilities() const override;
    bool getTransportStatus(chat::MeshTransportStatus*) const override;
    chat::MeshProtocol backendProtocol() const override;
    chat::NodeId getNodeId() const override;
    bool getReticulumLocalIdentityInfo(chat::ReticulumLocalIdentityInfo*) const override;
    bool requestDirectoryPath(const uint8_t destination[16]) override;
    chat::MeshActionResult startReticulumAudioCall(const chat::ReticulumPeerIdentity&) override;
    bool isReady() const override;
    bool isPkiReady() const override;
    bool hasPkiKey(chat::NodeId peer) const override;
    bool sendText(chat::ChannelId channel, const std::string& text, chat::MessageId* id, chat::NodeId peer = 0) override;
    bool sendTextWithId(chat::ChannelId channel, const std::string& text, chat::MessageId forced,
                        chat::MessageId* id, chat::NodeId peer = 0) override;
    chat::MeshSendResult sendTextDetailed(chat::ChannelId channel, const std::string& text,
                                          chat::MessageId forced = 0, chat::NodeId peer = 0) override;
    chat::MeshSendResult sendTextToReticulumDestination(chat::ChannelId, const std::string&, chat::MessageId,
                                                        const chat::ReticulumPeerIdentity&) override;
    bool sendAppData(chat::ChannelId channel, uint32_t port, const uint8_t* data, std::size_t size,
                     chat::NodeId peer = 0, bool ack = false, chat::MessageId packet = 0, bool response = false) override;
    bool pollIncomingText(chat::MeshIncomingText* out) override;
    chat::IIncomingDeliveryCommitPort* incomingDeliveryCommitPort() override { return this; }
    void commitIncomingText(const chat::MeshIncomingText&, bool durable) override;
    bool pollIncomingData(chat::MeshIncomingData* out) override;
    bool pollIncomingRawPacket(uint8_t* data, std::size_t& size, std::size_t capacity) override;
    bool requestNodeInfo(chat::NodeId peer, bool response) override;
    bool triggerDiscoveryAction(chat::MeshDiscoveryAction action) override;
    chat::MeshActionResult triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction action) override;
    bool startKeyVerification(chat::NodeId peer) override;
    bool submitKeyVerificationNumber(chat::NodeId peer, uint64_t nonce, uint32_t number) override;
    void processSendQueue() override;
    std::string statusText() const;
    std::string radioConfigText() const;
    std::string radioStatsText() const;
    std::vector<std::string> diagnosticLines() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace trailmate::linux_app
