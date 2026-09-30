#include "chat/infra/mesh_adapter_router_core.h"

namespace chat
{
namespace
{

uint8_t serviceBit(MeshProtocol protocol)
{
    switch (protocol)
    {
    case MeshProtocol::Meshtastic:
        return 1;
    case MeshProtocol::MeshCore:
        return 2;
    case MeshProtocol::RNode:
    case MeshProtocol::Reticulum:
        return 4;
    default:
        return 0;
    }
}

std::unique_ptr<IMeshAdapter>& backendSlot(MeshProtocol protocol,
                                           std::unique_ptr<IMeshAdapter>& meshtastic_backend,
                                           std::unique_ptr<IMeshAdapter>& meshcore_backend,
                                           std::unique_ptr<IMeshAdapter>& reticulum_backend)
{
    switch (protocol)
    {
    case MeshProtocol::MeshCore:
        return meshcore_backend;
    case MeshProtocol::RNode:
    case MeshProtocol::Reticulum:
        return reticulum_backend;
    case MeshProtocol::Meshtastic:
    default:
        return meshtastic_backend;
    }
}

const std::unique_ptr<IMeshAdapter>& backendSlotConst(MeshProtocol protocol,
                                                      const std::unique_ptr<IMeshAdapter>& meshtastic_backend,
                                                      const std::unique_ptr<IMeshAdapter>& meshcore_backend,
                                                      const std::unique_ptr<IMeshAdapter>& reticulum_backend)
{
    switch (protocol)
    {
    case MeshProtocol::MeshCore:
        return meshcore_backend;
    case MeshProtocol::RNode:
    case MeshProtocol::Reticulum:
        return reticulum_backend;
    case MeshProtocol::Meshtastic:
    default:
        return meshtastic_backend;
    }
}

} // namespace

bool MeshAdapterRouterCore::installBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend)
{
    if (!backend)
    {
        return false;
    }

    backendSlot(protocol, meshtastic_backend_, meshcore_backend_, reticulum_backend_) = std::move(backend);
    service_backend_mask_ &= static_cast<uint8_t>(~serviceBit(protocol));
    active_protocol_ = protocol;
    return true;
}

void MeshAdapterRouterCore::setActiveProtocol(MeshProtocol protocol)
{
    active_protocol_ = protocol;
}

bool MeshAdapterRouterCore::installServiceBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend)
{
    if (!backend || (protocol != MeshProtocol::Meshtastic && protocol != MeshProtocol::MeshCore &&
                     protocol != MeshProtocol::Reticulum && protocol != MeshProtocol::RNode))
    {
        return false;
    }
    auto& slot = backendSlot(protocol, meshtastic_backend_, meshcore_backend_, reticulum_backend_);
    if (slot)
    {
        return false;
    }
    slot = std::move(backend);
    service_backend_mask_ |= serviceBit(protocol);
    return true;
}

bool MeshAdapterRouterCore::hasBackend() const
{
    return activeBackend() != nullptr;
}

std::unique_ptr<IMeshAdapter> MeshAdapterRouterCore::takeServiceBackend(MeshProtocol protocol, const IMeshAdapter* expected)
{
    const auto bit = serviceBit(protocol);
    if (!bit || !(service_backend_mask_ & bit)) return {};
    return takeInactiveBackend(protocol, expected);
}

std::unique_ptr<IMeshAdapter> MeshAdapterRouterCore::takeInactiveBackend(MeshProtocol protocol, const IMeshAdapter* expected)
{
    const auto bit = serviceBit(protocol);
    if (!bit || serviceBit(active_protocol_) == bit) return {};
    auto& slot = backendSlot(protocol, meshtastic_backend_, meshcore_backend_, reticulum_backend_);
    if (expected && slot.get() != expected) return {};
    service_backend_mask_ &= static_cast<uint8_t>(~bit);
    return std::move(slot);
}

MeshProtocol MeshAdapterRouterCore::backendProtocol() const
{
    return active_protocol_;
}

IMeshAdapter* MeshAdapterRouterCore::backendForProtocol(MeshProtocol protocol)
{
    return backendSlot(protocol, meshtastic_backend_, meshcore_backend_, reticulum_backend_).get();
}

const IMeshAdapter* MeshAdapterRouterCore::backendForProtocol(MeshProtocol protocol) const
{
    return backendSlotConst(protocol, meshtastic_backend_, meshcore_backend_, reticulum_backend_).get();
}

MeshCapabilities MeshAdapterRouterCore::getCapabilities() const
{
    const IMeshAdapter* backend = activeBackend();
    return backend ? backend->getCapabilities() : MeshCapabilities{};
}

bool MeshAdapterRouterCore::sendText(ChannelId channel, const std::string& text,
                                     MessageId* out_msg_id, NodeId peer)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->sendText(channel, text, out_msg_id, peer);
}

bool MeshAdapterRouterCore::sendTextWithId(ChannelId channel, const std::string& text,
                                           MessageId forced_msg_id,
                                           MessageId* out_msg_id, NodeId peer)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->sendTextWithId(channel, text, forced_msg_id, out_msg_id, peer);
}

MeshSendResult MeshAdapterRouterCore::sendTextDetailed(ChannelId channel, const std::string& text,
                                                       MessageId forced_msg_id, NodeId peer)
{
    IMeshAdapter* backend = activeBackend();
    if (!backend)
    {
        return MeshSendResult::fail(MeshOperationFailure::NotReady);
    }
    return backend->sendTextDetailed(channel, text, forced_msg_id, peer);
}

MeshSendResult MeshAdapterRouterCore::sendTextToReticulumDestination(
    ChannelId channel,
    const std::string& text,
    MessageId forced_msg_id,
    const ReticulumPeerIdentity& destination)
{
    IMeshAdapter* backend = activeBackend();
    if (!backend)
    {
        return MeshSendResult::fail(MeshOperationFailure::NotReady);
    }
    return backend->sendTextToReticulumDestination(channel, text, forced_msg_id, destination);
}

bool MeshAdapterRouterCore::pollIncomingText(MeshIncomingText* out)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->pollIncomingText(out);
}

bool MeshAdapterRouterCore::sendAppData(ChannelId channel, uint32_t portnum,
                                        const uint8_t* payload, size_t len,
                                        NodeId dest, bool want_ack,
                                        MessageId packet_id,
                                        bool want_response)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->sendAppData(channel, portnum, payload, len, dest, want_ack,
                                           packet_id, want_response);
}

bool MeshAdapterRouterCore::pollIncomingData(MeshIncomingData* out)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->pollIncomingData(out);
}

bool MeshAdapterRouterCore::requestNodeInfo(NodeId dest, bool want_response)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->requestNodeInfo(dest, want_response);
}

bool MeshAdapterRouterCore::broadcastSelfIdentity()
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->broadcastSelfIdentity();
}

bool MeshAdapterRouterCore::startKeyVerification(NodeId dest)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->startKeyVerification(dest);
}

bool MeshAdapterRouterCore::submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->submitKeyVerificationNumber(dest, nonce, number);
}

NodeId MeshAdapterRouterCore::getNodeId() const
{
    const IMeshAdapter* backend = activeBackend();
    return backend ? backend->getNodeId() : 0;
}

bool MeshAdapterRouterCore::isPkiReady() const
{
    const IMeshAdapter* backend = activeBackend();
    return backend && backend->isPkiReady();
}

bool MeshAdapterRouterCore::getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const
{
    const IMeshAdapter* backend = activeBackend();
    if (!backend)
    {
        if (out)
        {
            *out = ReticulumLocalIdentityInfo{};
        }
        return false;
    }
    return backend->getReticulumLocalIdentityInfo(out);
}

bool MeshAdapterRouterCore::hasPkiKey(NodeId dest) const
{
    const IMeshAdapter* backend = activeBackend();
    return backend && backend->hasPkiKey(dest);
}

bool MeshAdapterRouterCore::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->triggerDiscoveryAction(action);
}

MeshActionResult MeshAdapterRouterCore::triggerDiscoveryActionDetailed(MeshDiscoveryAction action)
{
    IMeshAdapter* backend = activeBackend();
    if (!backend)
    {
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }
    return backend->triggerDiscoveryActionDetailed(action);
}

MeshActionResult MeshAdapterRouterCore::startReticulumAudioCall(
    const ReticulumPeerIdentity& destination)
{
    IMeshAdapter* backend = activeBackend();
    if (!backend)
    {
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }
    return backend->startReticulumAudioCall(destination);
}

MeshActionResult MeshAdapterRouterCore::pingReticulumDestination(
    const ReticulumPeerIdentity& destination)
{
    IMeshAdapter* backend = activeBackend();
    if (!backend)
    {
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }
    return backend->pingReticulumDestination(destination);
}

MeshActionResult MeshAdapterRouterCore::persistReticulumPeer(
    const ReticulumPeerIdentity& destination,
    bool favorite)
{
    IMeshAdapter* backend = activeBackend();
    if (!backend)
    {
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }
    return backend->persistReticulumPeer(destination, favorite);
}

void MeshAdapterRouterCore::applyConfig(const MeshConfig& config)
{
    IMeshAdapter* backend = activeBackend();
    if (backend)
    {
        backend->applyConfig(config);
    }
}

void MeshAdapterRouterCore::setUserInfo(const char* long_name, const char* short_name)
{
    IMeshAdapter* backend = activeBackend();
    if (backend)
    {
        backend->setUserInfo(long_name, short_name);
    }
}

void MeshAdapterRouterCore::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    IMeshAdapter* backend = activeBackend();
    if (backend)
    {
        backend->setNetworkLimits(duty_cycle_enabled, util_percent);
    }
}

void MeshAdapterRouterCore::setPrivacyConfig(uint8_t encrypt_mode)
{
    IMeshAdapter* backend = activeBackend();
    if (backend)
    {
        backend->setPrivacyConfig(encrypt_mode);
    }
}

bool MeshAdapterRouterCore::setWifiTransportEnabled(bool enabled)
{
    bool ok = true;
    IMeshAdapter* const backends[] = {
        meshtastic_backend_.get(),
        meshcore_backend_.get(),
        reticulum_backend_.get(),
    };
    for (IMeshAdapter* backend : backends)
    {
        if (backend && !backend->setWifiTransportEnabled(enabled))
        {
            ok = false;
        }
    }
    return ok;
}

bool MeshAdapterRouterCore::isReady() const
{
    const IMeshAdapter* backend = activeBackend();
    return backend && backend->isReady();
}

bool MeshAdapterRouterCore::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    IMeshAdapter* backend = activeBackend();
    return backend && backend->pollIncomingRawPacket(out_data, out_len, max_len);
}

void MeshAdapterRouterCore::handleRawPacket(const uint8_t* data, size_t size)
{
    IMeshAdapter* backend = activeBackend();
    if (backend)
    {
        backend->handleRawPacket(data, size);
    }
}

void MeshAdapterRouterCore::setLastRxStats(float rssi, float snr)
{
    IMeshAdapter* backend = activeBackend();
    if (backend)
    {
        backend->setLastRxStats(rssi, snr);
    }
}

void MeshAdapterRouterCore::processSendQueue()
{
    IMeshAdapter* backend = activeBackend();
    if (backend)
    {
        backend->processSendQueue();
    }
}

IMeshAdapter* MeshAdapterRouterCore::activeBackend()
{
    return backendForProtocol(active_protocol_);
}

bool MeshAdapterRouterCore::processServiceQueue(MeshProtocol protocol)
{
    if ((service_backend_mask_ & serviceBit(protocol)) == 0)
    {
        return false;
    }
    if (protocol != MeshProtocol::Meshtastic && protocol != MeshProtocol::MeshCore &&
        protocol != MeshProtocol::Reticulum && protocol != MeshProtocol::RNode)
    {
        return false;
    }
    IMeshAdapter* service = backendForProtocol(protocol);
    if (!service || service == activeBackend())
    {
        return false;
    }
    // A not-yet-ready service still needs polling to establish its IP path.
    service->processSendQueue();
    return true;
}

bool MeshAdapterRouterCore::isServiceBackend(MeshProtocol protocol) const
{
    return (service_backend_mask_ & serviceBit(protocol)) != 0 && backendForProtocol(protocol) != nullptr;
}

const IMeshAdapter* MeshAdapterRouterCore::activeBackend() const
{
    return backendForProtocol(active_protocol_);
}

} // namespace chat
