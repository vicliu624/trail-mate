/**
 * @file mesh_adapter_router.h
 * @brief Thread-safe mesh adapter router for runtime protocol switching
 */

#pragma once

#include <Arduino.h>

#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_adapter_router_core.h"
#include "chat/ports/i_mesh_adapter.h"
#include "freertos/semphr.h"

namespace chat
{

class MeshAdapterRouter : public IMeshAdapter
{
  public:
    MeshAdapterRouter();
    ~MeshAdapterRouter() override;

    bool installBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend) override;
    bool hasBackend() const override;
    MeshProtocol backendProtocol() const override;
    IMeshAdapter* backendForProtocol(MeshProtocol protocol) override;
    const IMeshAdapter* backendForProtocol(MeshProtocol protocol) const override;

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
    class LockGuard
    {
      public:
        explicit LockGuard(SemaphoreHandle_t mutex);
        ~LockGuard();
        bool locked() const { return locked_; }

      private:
        SemaphoreHandle_t mutex_ = nullptr;
        bool locked_ = false;
    };

    mutable SemaphoreHandle_t mutex_ = nullptr;
    chat::MeshAdapterRouterCore core_;
};

} // namespace chat
