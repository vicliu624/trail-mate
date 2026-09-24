/**
 * @file reticulum_adapter.h
 * @brief Product-level Reticulum adapter boundary for ESP Arduino targets.
 *
 * The current device-side Reticulum implementation uses the LXMF service layer
 * over the existing RNode-compatible raw carrier. Keep this header as the
 * product protocol entry point so factory code does not expose LXMF or RNode as
 * user-selectable protocols.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/ports/i_geocaching_transport.h"
#include "chat/ports/i_incoming_delivery_commit_port.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_mesh_peer_directory.h"

#include <memory>

class LoraBoard;

namespace chat::lxmf
{
class LxmfAdapter;
struct CustomDeliveryView;
struct GeocachingAnnouncementView;
} // namespace chat::lxmf

namespace chat::reticulum
{

namespace lxmf = ::chat::lxmf;

enum class ReticulumUsage : uint8_t
{
    ActiveChat,
    BackgroundIpService,
};

class ReticulumAdapter final : public IMeshAdapter,
                               public IGeocachingTransport,
                               public IIncomingDeliveryCommitPort
{
  public:
    explicit ReticulumAdapter(LoraBoard& board,
                              IMeshPeerDirectory* peer_directory = nullptr,
                              ReticulumUsage usage = ReticulumUsage::ActiveChat);
    ~ReticulumAdapter() override;

    ReticulumAdapter(const ReticulumAdapter&) = delete;
    ReticulumAdapter& operator=(const ReticulumAdapter&) = delete;

    MeshCapabilities getCapabilities() const override;
    IGeocachingTransport* geocachingTransport() override { return this; }
    bool getGeocachingAuthorKey(uint8_t out[64]) override;
    bool signGeocachingRecord(lxmf::ByteSpan record, uint8_t* workspace, size_t workspace_capacity,
                              uint8_t* output, size_t output_capacity, size_t& written) override;
    void setGeocachingAnnouncementHandler(void (*handler)(const lxmf::GeocachingAnnouncementView&, void*),
                                          void* context);
    MeshSendResult sendGeocachingData(const uint8_t destination_hash[16],
                                      lxmf::ByteSpan data, bool response = false,
                                      std::array<uint8_t, 32>* accepted_lxmf_hash = nullptr) override;
    void setGeocachingDeliveryHandler(bool (*handler)(const lxmf::CustomDeliveryView&, void*),
                                      void* context);
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    MeshSendResult sendTextDetailed(ChannelId channel, const std::string& text,
                                    MessageId forced_msg_id = 0,
                                    NodeId peer = 0) override;
    MeshSendResult sendTextToReticulumDestination(
        ChannelId channel,
        const std::string& text,
        MessageId forced_msg_id,
        const ReticulumPeerIdentity& destination) override;
    bool pollIncomingText(MeshIncomingText* out) override;
    IIncomingDeliveryCommitPort* incomingDeliveryCommitPort() override;
    void commitIncomingText(const MeshIncomingText& message,
                            bool durably_accepted) override;
    bool sendAppData(ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     NodeId dest = 0, bool want_ack = false,
                     MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(MeshIncomingData* out) override;
    bool requestNodeInfo(NodeId dest, bool want_response) override;
    bool broadcastSelfIdentity() override;
    NodeId getNodeId() const override;
    /** Delegates VMP secret derivation to the active LXMF identity service. */
    bool deriveVmpContactSecret(NodeId peer_id, uint8_t out_secret[32]);
    bool getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const override;
    MeshActionResult startReticulumAudioCall(
        const ReticulumPeerIdentity& destination) override;
    MeshActionResult pingReticulumDestination(
        const ReticulumPeerIdentity& destination) override;
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    bool setWifiTransportEnabled(bool enabled) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;

  private:
    std::unique_ptr<lxmf::LxmfAdapter> service_;
    const ReticulumUsage usage_;
};

} // namespace chat::reticulum
