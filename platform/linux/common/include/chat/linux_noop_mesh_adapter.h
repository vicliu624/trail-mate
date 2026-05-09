#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "chat/domain/chat_model.h"
#include "chat/ports/i_mesh_adapter.h"

namespace trailmate::cardputer_zero::linux_ui
{

// ---------------------------------------------------------------------------
// LinuxNoopMeshAdapter
//
// Minimal IMeshAdapter for DeviceLocal mode.  Does NOT inject synthetic
// peers, auto-replies, or loopback messages.  Returns false/no-op for all
// I/O operations.  Used when there is no real radio and we don't want the
// simulator's loopback world to leak into a real device.
// ---------------------------------------------------------------------------

class LinuxNoopMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    LinuxNoopMeshAdapter() = default;

    ::chat::MeshCapabilities getCapabilities() const override;
    bool sendText(::chat::ChannelId, const std::string&, ::chat::MessageId*,
                  ::chat::NodeId = 0) override;
    bool sendTextWithId(::chat::ChannelId, const std::string&, ::chat::MessageId,
                        ::chat::MessageId*, ::chat::NodeId = 0) override;
    bool pollIncomingText(::chat::MeshIncomingText*) override;
    bool sendAppData(::chat::ChannelId, uint32_t, const uint8_t*, size_t,
                     ::chat::NodeId = 0, bool = false, ::chat::MessageId = 0,
                     bool = false) override;
    bool pollIncomingData(::chat::MeshIncomingData*) override;
    bool requestNodeInfo(::chat::NodeId, bool = false) override;
    bool startKeyVerification(::chat::NodeId) override;
    bool submitKeyVerificationNumber(::chat::NodeId, uint64_t, uint32_t) override;
    ::chat::NodeId getNodeId() const override;
    bool isPkiReady() const override;
    bool hasPkiKey(::chat::NodeId) const override;
    bool triggerDiscoveryAction(::chat::MeshDiscoveryAction) override;
    void applyConfig(const ::chat::MeshConfig&) override;
    void setUserInfo(const char*, const char*) override;
    bool isReady() const override;
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override;

    // Extra methods used by MinimalLinuxAppFacade internals (not on IMeshAdapter).
    void setSelfNodeId(::chat::NodeId id) { self_id_ = id; }
    void tick() {}
    bool takePendingSendResult(::chat::MessageId&, bool&) { return false; }
    void queueIncomingText(const ::chat::MeshIncomingText&) {}
    void queueIncomingData(const ::chat::MeshIncomingData&) {}

  private:
    ::chat::NodeId self_id_{0};
};

} // namespace trailmate::cardputer_zero::linux_ui
