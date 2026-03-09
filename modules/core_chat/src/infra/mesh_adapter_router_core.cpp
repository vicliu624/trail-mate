#include "chat/infra/mesh_adapter_router_core.h"

namespace chat
{

bool MeshAdapterRouterCore::installBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend)
{
    if (!backend)
    {
        return false;
    }
    backend_ = std::move(backend);
    backend_protocol_ = protocol;
    return true;
}

bool MeshAdapterRouterCore::hasBackend() const
{
    return static_cast<bool>(backend_);
}

MeshProtocol MeshAdapterRouterCore::backendProtocol() const
{
    return backend_protocol_;
}

IMeshAdapter* MeshAdapterRouterCore::backendForProtocol(MeshProtocol protocol)
{
    if (!backend_ || backend_protocol_ != protocol)
    {
        return nullptr;
    }
    return backend_.get();
}

const IMeshAdapter* MeshAdapterRouterCore::backendForProtocol(MeshProtocol protocol) const
{
    if (!backend_ || backend_protocol_ != protocol)
    {
        return nullptr;
    }
    return backend_.get();
}

MeshCapabilities MeshAdapterRouterCore::getCapabilities() const
{
    return backend_ ? backend_->getCapabilities() : MeshCapabilities{};
}

bool MeshAdapterRouterCore::sendText(ChannelId channel, const std::string& text,
                                     MessageId* out_msg_id, NodeId peer)
{
    return backend_ && backend_->sendText(channel, text, out_msg_id, peer);
}

bool MeshAdapterRouterCore::pollIncomingText(MeshIncomingText* out)
{
    return backend_ && backend_->pollIncomingText(out);
}

bool MeshAdapterRouterCore::sendAppData(ChannelId channel, uint32_t portnum,
                                        const uint8_t* payload, size_t len,
                                        NodeId dest, bool want_ack,
                                        MessageId packet_id,
                                        bool want_response)
{
    return backend_ && backend_->sendAppData(channel, portnum, payload, len, dest, want_ack,
                                             packet_id, want_response);
}

bool MeshAdapterRouterCore::pollIncomingData(MeshIncomingData* out)
{
    return backend_ && backend_->pollIncomingData(out);
}

bool MeshAdapterRouterCore::requestNodeInfo(NodeId dest, bool want_response)
{
    return backend_ && backend_->requestNodeInfo(dest, want_response);
}

bool MeshAdapterRouterCore::startKeyVerification(NodeId dest)
{
    return backend_ && backend_->startKeyVerification(dest);
}

bool MeshAdapterRouterCore::submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number)
{
    return backend_ && backend_->submitKeyVerificationNumber(dest, nonce, number);
}

NodeId MeshAdapterRouterCore::getNodeId() const
{
    return backend_ ? backend_->getNodeId() : 0;
}

bool MeshAdapterRouterCore::isPkiReady() const
{
    return backend_ && backend_->isPkiReady();
}

bool MeshAdapterRouterCore::hasPkiKey(NodeId dest) const
{
    return backend_ && backend_->hasPkiKey(dest);
}

bool MeshAdapterRouterCore::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    return backend_ && backend_->triggerDiscoveryAction(action);
}

void MeshAdapterRouterCore::applyConfig(const MeshConfig& config)
{
    if (backend_)
    {
        backend_->applyConfig(config);
    }
}

void MeshAdapterRouterCore::setUserInfo(const char* long_name, const char* short_name)
{
    if (backend_)
    {
        backend_->setUserInfo(long_name, short_name);
    }
}

void MeshAdapterRouterCore::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    if (backend_)
    {
        backend_->setNetworkLimits(duty_cycle_enabled, util_percent);
    }
}

void MeshAdapterRouterCore::setPrivacyConfig(uint8_t encrypt_mode, bool pki_enabled)
{
    if (backend_)
    {
        backend_->setPrivacyConfig(encrypt_mode, pki_enabled);
    }
}

bool MeshAdapterRouterCore::isReady() const
{
    return backend_ && backend_->isReady();
}

bool MeshAdapterRouterCore::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    return backend_ && backend_->pollIncomingRawPacket(out_data, out_len, max_len);
}

void MeshAdapterRouterCore::handleRawPacket(const uint8_t* data, size_t size)
{
    if (backend_)
    {
        backend_->handleRawPacket(data, size);
    }
}

void MeshAdapterRouterCore::setLastRxStats(float rssi, float snr)
{
    if (backend_)
    {
        backend_->setLastRxStats(rssi, snr);
    }
}

void MeshAdapterRouterCore::processSendQueue()
{
    if (backend_)
    {
        backend_->processSendQueue();
    }
}

} // namespace chat
