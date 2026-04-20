#pragma once

#include "../domain/chat_types.h"
#include "../ports/i_mesh_adapter.h"
#include <memory>

namespace chat
{

class MeshAdapterRouterCore : public IMeshAdapter
{
  public:
    bool installBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend);
    void setActiveProtocol(MeshProtocol protocol);
    bool hasBackend() const;
    MeshProtocol backendProtocol() const;
    IMeshAdapter* backendForProtocol(MeshProtocol protocol);
    const IMeshAdapter* backendForProtocol(MeshProtocol protocol) const;

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
    bool startKeyVerification(NodeId dest) override;
    bool submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number) override;
    NodeId getNodeId() const override;
    bool isPkiReady() const override;
    bool hasPkiKey(NodeId dest) const override;
    bool triggerDiscoveryAction(MeshDiscoveryAction action) override;
    void applyConfig(const MeshConfig& config) override;
    void setUserInfo(const char* long_name, const char* short_name) override;
    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override;
    void setPrivacyConfig(uint8_t encrypt_mode) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override;
    void handleRawPacket(const uint8_t* data, size_t size) override;
    void setLastRxStats(float rssi, float snr) override;
    void processSendQueue() override;

  private:
    IMeshAdapter* activeBackend();
    const IMeshAdapter* activeBackend() const;

    std::unique_ptr<IMeshAdapter> meshtastic_backend_;
    std::unique_ptr<IMeshAdapter> meshcore_backend_;
    MeshProtocol active_protocol_ = MeshProtocol::Meshtastic;
};

} // namespace chat
