#pragma once

#include "chat/ports/i_mesh_adapter.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/runtime/self_identity_provider.h"

#include <queue>
#include <string>

namespace platform::nrf52::arduino_common::chat::meshtastic
{

class MtAdapterLite final : public ::chat::IMeshAdapter
{
  public:
    explicit MtAdapterLite(const ::chat::runtime::SelfIdentityProvider* identity_provider = nullptr);

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
    void applyConfig(const ::chat::MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode, bool pki_enabled) override;
    bool isReady() const override;
    ::chat::NodeId getNodeId() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;

  private:
    ::chat::runtime::EffectiveSelfIdentity buildEffectiveIdentity() const;
    bool transmitWire(const uint8_t* data, size_t size);
    bool buildAndQueueNodeInfo(::chat::NodeId dest, bool want_response);

    ::chat::MeshConfig config_{};
    ::chat::NodeId node_id_ = 0;
    ::chat::MessageId next_packet_id_ = 1;
    std::string long_name_;
    std::string short_name_;
    const ::chat::runtime::SelfIdentityProvider* identity_provider_ = nullptr;
    float last_rx_rssi_ = 0.0f;
    float last_rx_snr_ = 0.0f;
    std::queue<::chat::MeshIncomingText> text_queue_;
    std::queue<::chat::MeshIncomingData> data_queue_;
};

} // namespace platform::nrf52::arduino_common::chat::meshtastic
