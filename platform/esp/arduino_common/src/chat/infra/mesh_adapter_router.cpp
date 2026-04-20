/**
 * @file mesh_adapter_router.cpp
 * @brief Thread-safe mesh adapter router for runtime protocol switching
 */

#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"

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
    LockGuard lock(mutex_);
    return lock.locked() && core_.installBackend(protocol, std::move(backend));
}

bool MeshAdapterRouter::hasBackend() const
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.hasBackend();
}

MeshProtocol MeshAdapterRouter::backendProtocol() const
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.backendProtocol() : MeshProtocol::Meshtastic;
}

IMeshAdapter* MeshAdapterRouter::backendForProtocol(MeshProtocol protocol)
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.backendForProtocol(protocol) : nullptr;
}

const IMeshAdapter* MeshAdapterRouter::backendForProtocol(MeshProtocol protocol) const
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.backendForProtocol(protocol) : nullptr;
}

MeshCapabilities MeshAdapterRouter::getCapabilities() const
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.getCapabilities() : MeshCapabilities{};
}

bool MeshAdapterRouter::sendText(ChannelId channel, const std::string& text,
                                 MessageId* out_msg_id, NodeId peer)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.sendText(channel, text, out_msg_id, peer);
}

bool MeshAdapterRouter::sendTextWithId(ChannelId channel, const std::string& text,
                                       MessageId forced_msg_id,
                                       MessageId* out_msg_id, NodeId peer)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.sendTextWithId(channel, text, forced_msg_id, out_msg_id, peer);
}

bool MeshAdapterRouter::pollIncomingText(MeshIncomingText* out)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.pollIncomingText(out);
}

bool MeshAdapterRouter::sendAppData(ChannelId channel, uint32_t portnum,
                                    const uint8_t* payload, size_t len,
                                    NodeId dest, bool want_ack,
                                    MessageId packet_id,
                                    bool want_response)
{
    LockGuard lock(mutex_);
    return lock.locked() &&
           core_.sendAppData(channel, portnum, payload, len, dest, want_ack, packet_id, want_response);
}

bool MeshAdapterRouter::pollIncomingData(MeshIncomingData* out)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.pollIncomingData(out);
}

bool MeshAdapterRouter::requestNodeInfo(NodeId dest, bool want_response)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.requestNodeInfo(dest, want_response);
}

bool MeshAdapterRouter::startKeyVerification(NodeId dest)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.startKeyVerification(dest);
}

bool MeshAdapterRouter::submitKeyVerificationNumber(NodeId dest, uint64_t nonce, uint32_t number)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.submitKeyVerificationNumber(dest, nonce, number);
}

NodeId MeshAdapterRouter::getNodeId() const
{
    LockGuard lock(mutex_);
    return lock.locked() ? core_.getNodeId() : 0;
}

bool MeshAdapterRouter::isPkiReady() const
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.isPkiReady();
}

bool MeshAdapterRouter::hasPkiKey(NodeId dest) const
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.hasPkiKey(dest);
}

bool MeshAdapterRouter::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.triggerDiscoveryAction(action);
}

void MeshAdapterRouter::applyConfig(const MeshConfig& config)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.applyConfig(config);
    }
}

void MeshAdapterRouter::setUserInfo(const char* long_name, const char* short_name)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.setUserInfo(long_name, short_name);
    }
}

void MeshAdapterRouter::setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.setNetworkLimits(duty_cycle_enabled, util_percent);
    }
}

void MeshAdapterRouter::setPrivacyConfig(uint8_t encrypt_mode)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.setPrivacyConfig(encrypt_mode);
    }
}

bool MeshAdapterRouter::isReady() const
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.isReady();
}

bool MeshAdapterRouter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    LockGuard lock(mutex_);
    return lock.locked() && core_.pollIncomingRawPacket(out_data, out_len, max_len);
}

void MeshAdapterRouter::handleRawPacket(const uint8_t* data, size_t size)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.handleRawPacket(data, size);
    }
}

void MeshAdapterRouter::setLastRxStats(float rssi, float snr)
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.setLastRxStats(rssi, snr);
    }
}

void MeshAdapterRouter::processSendQueue()
{
    LockGuard lock(mutex_);
    if (lock.locked())
    {
        core_.processSendQueue();
    }
}

} // namespace chat
