#include "chat/linux_noop_mesh_adapter.h"

namespace trailmate::cardputer_zero::linux_ui
{

::chat::MeshCapabilities LinuxNoopMeshAdapter::getCapabilities() const
{
    return {};
}

bool LinuxNoopMeshAdapter::sendText(::chat::ChannelId, const std::string&,
                                    ::chat::MessageId*, ::chat::NodeId)
{
    return false;
}

bool LinuxNoopMeshAdapter::sendTextWithId(::chat::ChannelId, const std::string&,
                                          ::chat::MessageId, ::chat::MessageId*,
                                          ::chat::NodeId)
{
    return false;
}

bool LinuxNoopMeshAdapter::pollIncomingText(::chat::MeshIncomingText*)
{
    return false;
}

bool LinuxNoopMeshAdapter::sendAppData(::chat::ChannelId, uint32_t,
                                       const uint8_t*, size_t, ::chat::NodeId,
                                       bool, ::chat::MessageId, bool)
{
    return false;
}

bool LinuxNoopMeshAdapter::pollIncomingData(::chat::MeshIncomingData*)
{
    return false;
}

bool LinuxNoopMeshAdapter::requestNodeInfo(::chat::NodeId, bool)
{
    return false;
}

bool LinuxNoopMeshAdapter::startKeyVerification(::chat::NodeId)
{
    return false;
}

bool LinuxNoopMeshAdapter::submitKeyVerificationNumber(::chat::NodeId,
                                                       uint64_t, uint32_t)
{
    return false;
}

::chat::NodeId LinuxNoopMeshAdapter::getNodeId() const
{
    return self_id_;
}

bool LinuxNoopMeshAdapter::isPkiReady() const
{
    return false;
}

bool LinuxNoopMeshAdapter::hasPkiKey(::chat::NodeId) const
{
    return false;
}

bool LinuxNoopMeshAdapter::triggerDiscoveryAction(::chat::MeshDiscoveryAction)
{
    return false;
}

void LinuxNoopMeshAdapter::applyConfig(const ::chat::MeshConfig&) {}

void LinuxNoopMeshAdapter::setUserInfo(const char*, const char*) {}

bool LinuxNoopMeshAdapter::isReady() const
{
    return true;
}

bool LinuxNoopMeshAdapter::pollIncomingRawPacket(uint8_t*, size_t&, size_t)
{
    return false;
}

} // namespace trailmate::cardputer_zero::linux_ui
