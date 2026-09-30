/**
 * @file mesh_adapter_router.h
 * @brief Thread-safe mesh adapter router for runtime protocol switching
 */

#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/lxmf/lxmf_wire.h"
#include "chat/infra/mesh_adapter_router_core.h"
#include "chat/ports/i_incoming_delivery_commit_port.h"
#include "chat/ports/i_mesh_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <array>

namespace chat
{
namespace lxmf
{
struct GeocachingAnnouncementView;
struct CustomDeliveryView;
} // namespace lxmf

class MeshAdapterRouter : public IMeshAdapter,
                          public IIncomingDeliveryCommitPort
{
  public:
    MeshAdapterRouter();
    ~MeshAdapterRouter() override;

    bool installBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend) override;
    bool installServiceBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend);
    std::unique_ptr<IMeshAdapter> takeServiceBackend(MeshProtocol protocol, const IMeshAdapter* expected = nullptr);
    // Retire a cached former chat instance, never an active/shared service.
    std::unique_ptr<IMeshAdapter> takeInactiveReticulumCache();
    bool processServiceQueue(MeshProtocol protocol);
    // Worker-thread call. Resolves the current instance while holding the
    // router lock; callers must not retain a backend pointer across switches.
    MeshSendResult sendGeocachingData(const uint8_t destination_hash[16],
                                      lxmf::ByteSpan data, bool response = false,
                                      std::array<uint8_t, 32>* accepted_lxmf_hash = nullptr,
                                      const uint8_t expected_source[16] = nullptr);
    bool getGeocachingDispatchDestination(uint8_t out[16]);
    bool getGeocachingAuthorKey(uint8_t out[64]);
    // The storage owner must reserve the author revision before publication.
    bool signGeocachingRecord(lxmf::ByteSpan record, uint8_t* workspace, size_t workspace_capacity,
                              uint8_t* output, size_t output_capacity, size_t& written);
    // Callbacks run under the router lock: enqueue/copy bounded work, never
    // reenter the router. Delivery true means durable acceptance, not queued
    // volatile work. Unbind before destroying the context object.
    bool bindGeocachingHandlers(void (*announcement)(const lxmf::GeocachingAnnouncementView&, void*),
                                bool (*delivery)(const lxmf::CustomDeliveryView&, void*),
                                void* context);
    bool hasBackend() const override;
    MeshProtocol backendProtocol() const override;
    IMeshAdapter* backendForProtocol(MeshProtocol protocol) override;
    const IMeshAdapter* backendForProtocol(MeshProtocol protocol) const override;

    /**
     * Pager-only bridge to a VMP-domain-separated secret of the active,
     * verified contact identity. This deliberately is not part of IMeshAdapter.
     */
    bool deriveVmpContactSecret(NodeId peer_id, uint8_t out_secret[32]);

    MeshCapabilities getCapabilities() const override;
    bool sendText(ChannelId channel, const std::string& text,
                  MessageId* out_msg_id, NodeId peer = 0) override;
    bool sendTextWithId(ChannelId channel, const std::string& text,
                        MessageId forced_msg_id,
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
    bool startKeyVerification(NodeId dest) override;
    bool submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number) override;
    NodeId getNodeId() const override;
    bool isPkiReady() const override;
    bool getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const override;
    bool hasPkiKey(NodeId dest) const override;
    bool triggerDiscoveryAction(MeshDiscoveryAction action) override;
    MeshActionResult triggerDiscoveryActionDetailed(MeshDiscoveryAction action) override;
    MeshActionResult startReticulumAudioCall(
        const ReticulumPeerIdentity& destination) override;
    MeshActionResult pingReticulumDestination(
        const ReticulumPeerIdentity& destination) override;
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode) override;
    bool setWifiTransportEnabled(bool enabled) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;

  private:
    class LockGuard
    {
      public:
        explicit LockGuard(SemaphoreHandle_t mutex,
                           TickType_t wait_ticks = portMAX_DELAY);
        ~LockGuard();
        bool locked() const { return locked_; }

      private:
        SemaphoreHandle_t mutex_ = nullptr;
        bool locked_ = false;
    };

    mutable SemaphoreHandle_t mutex_ = nullptr;
    chat::MeshAdapterRouterCore core_;
    IGeocachingTransport* geocachingTransportLocked();
    bool geocachingDestinationLocked(uint8_t out[16]);
    void applyGeocachingHandlers(MeshProtocol protocol, IMeshAdapter* backend);
    void (*geocaching_announcement_)(const lxmf::GeocachingAnnouncementView&, void*) = nullptr;
    bool (*geocaching_delivery_)(const lxmf::CustomDeliveryView&, void*) = nullptr;
    void* geocaching_context_ = nullptr;
};

} // namespace chat
