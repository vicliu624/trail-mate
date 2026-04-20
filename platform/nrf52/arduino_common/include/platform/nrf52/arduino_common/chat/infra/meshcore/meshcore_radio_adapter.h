#pragma once

#include "chat/infra/meshcore/meshcore_ble_backend.h"
#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/runtime/self_identity_provider.h"

#include <array>
#include <queue>
#include <string>

namespace platform::nrf52::arduino_common::chat::meshcore
{

class MeshCoreRadioAdapter final : public ::chat::IMeshAdapter, public ::chat::meshcore::IMeshCoreBleBackend
{
  public:
    explicit MeshCoreRadioAdapter(const ::chat::runtime::SelfIdentityProvider* identity_provider = nullptr);

    ::chat::MeshCapabilities getCapabilities() const override;
    bool sendText(::chat::ChannelId channel, const std::string& text,
                  ::chat::MessageId* out_msg_id, ::chat::NodeId peer = 0) override;
    bool pollIncomingText(::chat::MeshIncomingText* out) override;
    bool sendAppData(::chat::ChannelId channel, uint32_t portnum,
                     const uint8_t* payload, size_t len,
                     ::chat::NodeId dest = 0, bool want_ack = false,
                     ::chat::MessageId packet_id = 0,
                     bool want_response = false) override;
    bool pollIncomingData(::chat::MeshIncomingData* out) override;
    bool requestNodeInfo(::chat::NodeId dest, bool want_response) override;
    bool triggerDiscoveryAction(::chat::MeshDiscoveryAction action) override;
    void applyConfig(const ::chat::MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode) override;
    bool isReady() const override;
    ::chat::NodeId getNodeId() const override;
    ::chat::meshcore::IMeshCoreBleBackend* asMeshCoreBleBackend() override { return this; }
    const ::chat::meshcore::IMeshCoreBleBackend* asMeshCoreBleBackend() const override { return this; }
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;
    bool exportIdentityPublicKey(uint8_t out_pubkey[::chat::meshcore::kMeshCorePubKeySize]) override;
    bool exportIdentityPrivateKey(uint8_t out_priv[::chat::meshcore::kMeshCorePrivKeySize]) override;
    bool signPayload(const uint8_t* payload, size_t len,
                     uint8_t out_signature[::chat::meshcore::kMeshCoreSignatureSize]) override;
    bool exportIdentityPublicKey(uint8_t* out_key, size_t out_len);
    bool exportIdentityPrivateKey(uint8_t* out_key, size_t out_len);
    bool importIdentityPrivateKey(const uint8_t* key, size_t len);
    bool signPayload(const uint8_t* payload, size_t len, uint8_t* out_signature, size_t out_len);
    bool sendSelfAdvert(bool broadcast);
    bool sendPeerRequestType(const uint8_t* pubkey, size_t len, uint8_t req_type,
                             uint32_t* out_tag, uint32_t* out_est_timeout,
                             bool* out_sent_flood);
    bool sendPeerRequestPayload(const uint8_t* pubkey, size_t len,
                                const uint8_t* payload, size_t payload_len,
                                bool force_flood,
                                uint32_t* out_tag, uint32_t* out_est_timeout,
                                bool* out_sent_flood);
    bool sendAnonRequestPayload(const uint8_t* pubkey, size_t len,
                                const uint8_t* payload, size_t payload_len,
                                uint32_t* out_est_timeout,
                                bool* out_sent_flood);
    bool sendTracePath(const uint8_t* path, size_t path_len,
                       uint32_t tag, uint32_t auth, uint8_t flags,
                       uint32_t* out_est_timeout);
    bool sendControlData(const uint8_t* payload, size_t payload_len);
    bool sendRawData(const uint8_t* path, size_t path_len,
                     const uint8_t* payload, size_t payload_len,
                     uint32_t* out_est_timeout);
    void setFloodScopeKey(const uint8_t* key, size_t len);

  private:
    ::chat::runtime::EffectiveSelfIdentity buildEffectiveIdentity() const;
    void ensureIdentityKeys();
    bool transmitFrame(const uint8_t* data, size_t size);
    bool sendAdvert(bool broadcast);

    ::chat::MeshConfig config_{};
    ::chat::NodeId node_id_ = 0;
    std::string long_name_;
    std::string short_name_;
    const ::chat::runtime::SelfIdentityProvider* identity_provider_ = nullptr;
    bool keys_ready_ = false;
    uint8_t public_key_[::chat::meshcore::kMeshCorePubKeySize] = {};
    uint8_t private_key_[::chat::meshcore::kMeshCorePrivKeySize] = {};
    std::array<uint8_t, 16> flood_scope_key_{};
    std::queue<::chat::MeshIncomingText> text_queue_;
    std::queue<::chat::MeshIncomingData> data_queue_;
};

} // namespace platform::nrf52::arduino_common::chat::meshcore
