/**
 * @file mesh_adapter_router.cpp
 * @brief Thread-safe mesh adapter router for runtime protocol switching
 */

#include "mesh_adapter_router.h"

namespace chat
{

MeshAdapterRouter::LockGuard::LockGuard(SemaphoreHandle_t mutex) : mutex_(mutex)
{
    if (mutex_ != nullptr)
    {
        locked_ = (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE);
    }
}

MeshAdapterRouter::LockGuard::~LockGuard()
{
    if (locked_ && mutex_ != nullptr)
    {
        xSemaphoreGive(mutex_);
    }
}

MeshAdapterRouter::MeshAdapterRouter()
{
    mutex_ = xSemaphoreCreateMutex();
}

MeshAdapterRouter::~MeshAdapterRouter()
{
    if (mutex_ != nullptr)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

bool MeshAdapterRouter::installBackend(MeshProtocol protocol, std::unique_ptr<IMeshAdapter> backend)
{
    if (!backend)
    {
        return false;
    }
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return false;
    }
    backend_ = std::move(backend);
    backend_protocol_ = protocol;
    return true;
}

bool MeshAdapterRouter::hasBackend() const
{
    LockGuard lock(mutex_);
    return lock.locked() && static_cast<bool>(backend_);
}

MeshProtocol MeshAdapterRouter::backendProtocol() const
{
    LockGuard lock(mutex_);
    if (!lock.locked())
    {
        return MeshProtocol::Meshtastic;
    }
    return backend_protocol_;
}

IMeshAdapter* MeshAdapterRouter::backendForProtocol(MeshProtocol protocol)
{
    LockGuard lock(mutex_);
    if (!lock.locked() || !backend_ || backend_protocol_ != protocol)
    {
        return nullptr;
    }
    return backend_.get();
}

const IMeshAdapter* MeshAdapterRouter::backendForProtocol(MeshProtocol protocol) const
{
    LockGuard lock(mutex_);
    if (!lock.locked() || !backend_ || backend_protocol_ != protocol)
    {
        return nullptr;
    }
    return backend_.get();
}

MeshCapabilities MeshAdapterRouter::getCapabilities() const
{
    LockGuard lock(mutex_);
    if (!lock.locked() || !backend_)
    {
        return MeshCapabilities{};
    }
    return backend_->getCapabilities();
}

bool MeshAdapterRouter::sendText(ChannelId channel, const std::string& text,
                                 MessageId* out_msg_id, NodeId peer)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->sendText(channel, text, out_msg_id, peer);
}

bool MeshAdapterRouter::pollIncomingText(MeshIncomingText* out)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->pollIncomingText(out);
}

bool MeshAdapterRouter::sendAppData(ChannelId channel, uint32_t portnum,
                                    const uint8_t* payload, size_t len,
                                    NodeId dest, bool want_ack,
                                    MessageId packet_id,
                                    bool want_response)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ &&
           backend_->sendAppData(channel, portnum, payload, len, dest, want_ack,
                                 packet_id, want_response);
}

bool MeshAdapterRouter::pollIncomingData(MeshIncomingData* out)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->pollIncomingData(out);
}

bool MeshAdapterRouter::requestNodeInfo(NodeId dest, bool want_response)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->requestNodeInfo(dest, want_response);
}

bool MeshAdapterRouter::startKeyVerification(NodeId dest)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->startKeyVerification(dest);
}

bool MeshAdapterRouter::submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ &&
           backend_->submitKeyVerificationNumber(dest, nonce, number);
}

NodeId MeshAdapterRouter::getNodeId() const
{
    LockGuard lock(mutex_);
    if (!lock.locked() || !backend_)
    {
        return 0;
    }
    return backend_->getNodeId();
}

bool MeshAdapterRouter::isPkiReady() const
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->isPkiReady();
}

bool MeshAdapterRouter::hasPkiKey(NodeId dest) const
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->hasPkiKey(dest);
}

bool MeshAdapterRouter::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->triggerDiscoveryAction(action);
}

void MeshAdapterRouter::applyConfig(const MeshConfig& config)
{
    LockGuard lock(mutex_);
    if (lock.locked() && backend_)
    {
        backend_->applyConfig(config);
    }
}

void MeshAdapterRouter::setUserInfo(const char* long_name, const char* short_name)
{
    LockGuard lock(mutex_);
    if (lock.locked() && backend_)
    {
        backend_->setUserInfo(long_name, short_name);
    }
}

void MeshAdapterRouter::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    LockGuard lock(mutex_);
    if (lock.locked() && backend_)
    {
        backend_->setNetworkLimits(duty_cycle_enabled, util_percent);
    }
}

void MeshAdapterRouter::setPrivacyConfig(uint8_t encrypt_mode, bool pki_enabled)
{
    LockGuard lock(mutex_);
    if (lock.locked() && backend_)
    {
        backend_->setPrivacyConfig(encrypt_mode, pki_enabled);
    }
}

bool MeshAdapterRouter::isReady() const
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ && backend_->isReady();
}

bool MeshAdapterRouter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    LockGuard lock(mutex_);
    return lock.locked() && backend_ &&
           backend_->pollIncomingRawPacket(out_data, out_len, max_len);
}

void MeshAdapterRouter::handleRawPacket(const uint8_t* data, size_t size)
{
    LockGuard lock(mutex_);
    if (lock.locked() && backend_)
    {
        backend_->handleRawPacket(data, size);
    }
}

void MeshAdapterRouter::setLastRxStats(float rssi, float snr)
{
    LockGuard lock(mutex_);
    if (lock.locked() && backend_)
    {
        backend_->setLastRxStats(rssi, snr);
    }
}

void MeshAdapterRouter::processSendQueue()
{
    LockGuard lock(mutex_);
    if (lock.locked() && backend_)
    {
        backend_->processSendQueue();
    }
}

} // namespace chat
