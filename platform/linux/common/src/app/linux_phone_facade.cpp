#include "app/linux_phone_facade.h"
#include "app/linux_app_services.h"
#include "chat/infra/meshcore/meshcore_ble_backend.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/shared/ble/app_config_phone_snapshot_bridge.h"
#include "platform/ui/time_runtime.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace trailmate::linux_app
{
namespace
{
chat::contacts::NodeProtocolType nodeProtocol(chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case chat::MeshProtocol::Meshtastic:
        return chat::contacts::NodeProtocolType::Meshtastic;
    case chat::MeshProtocol::MeshCore:
        return chat::contacts::NodeProtocolType::MeshCore;
    case chat::MeshProtocol::Reticulum:
        return chat::contacts::NodeProtocolType::Reticulum;
    default:
        return chat::contacts::NodeProtocolType::Unknown;
    }
}
void nodeView(const chat::contacts::PeerDirectoryItem& entry, phone::PhoneNodeView& out)
{
    out = {};
    out.node_id = entry.node_id;
    platform::shared::ble_bridge::copyBounded(out.short_name, sizeof(out.short_name), entry.short_name);
    platform::shared::ble_bridge::copyBounded(out.long_name, sizeof(out.long_name), entry.long_name);
    out.last_seen = entry.last_seen;
    out.snr = entry.snr;
    out.rssi = entry.rssi;
    out.hops_away = entry.hops_away;
    out.channel = entry.channel;
    out.next_hop = entry.next_hop;
    out.protocol = static_cast<uint8_t>(entry.protocol);
    out.role = static_cast<uint8_t>(entry.role);
    out.hw_model = entry.hw_model;
    out.has_macaddr = entry.has_macaddr;
    std::copy_n(entry.macaddr, 6, out.macaddr);
    out.via_mqtt = entry.via_mqtt;
    out.is_ignored = entry.is_ignored;
    out.has_public_key = entry.has_public_key;
    out.key_manually_verified = entry.key_manually_verified;
    out.has_device_metrics = entry.has_device_metrics;
    out.device_metrics = entry.device_metrics;
    out.position = entry.position;
}
} // namespace
LinuxPhoneFacade::LinuxPhoneFacade(LinuxAppServices& services) : services_(services) {}
void LinuxPhoneFacade::setDeviceMac(const std::array<uint8_t, 6>& address)
{
    mac_ = address;
    has_mac_ = std::any_of(mac_.begin(), mac_.end(), [](uint8_t byte)
                           { return byte != 0; });
}
bool LinuxPhoneFacade::enqueueIncomingData(const chat::MeshIncomingData& data)
{
    if (incoming_.size() >= 16) return false; // Drop newest; never steal another consumer's input.
    incoming_.push_back(data);
    return true;
}
void LinuxPhoneFacade::clearIncomingData() { incoming_.clear(); }
phone::MeshtasticPhoneConfigSnapshot LinuxPhoneFacade::getMeshtasticPhoneConfig() const
{
    return platform::shared::ble_bridge::makeMeshtasticPhoneConfigSnapshot(services_.config());
}
void LinuxPhoneFacade::setMeshtasticPhoneConfig(const phone::MeshtasticPhoneConfigSnapshot& snapshot)
{
    platform::shared::ble_bridge::applyMeshtasticPhoneConfigSnapshot(services_.config(), snapshot);
}
phone::MeshCorePhoneConfigSnapshot LinuxPhoneFacade::getMeshCorePhoneConfig() const
{
    return platform::shared::ble_bridge::makeMeshCorePhoneConfigSnapshot(services_.config());
}
void LinuxPhoneFacade::setMeshCorePhoneConfig(const phone::MeshCorePhoneConfigSnapshot& snapshot)
{
    // Protocol switching is owned by composition, never an incidental config copy.
    auto current = snapshot;
    current.active_protocol = services_.meshProtocol();
    platform::shared::ble_bridge::applyMeshCorePhoneConfigSnapshot(services_.config(), current);
}
void LinuxPhoneFacade::saveConfig() { services_.saveConfig(); }
void LinuxPhoneFacade::applyMeshConfig() { services_.applyMeshConfig(); }
void LinuxPhoneFacade::applyUserInfo() { services_.applyUserInfo(); }
void LinuxPhoneFacade::applyPositionConfig() { services_.applyPositionConfig(); }
void LinuxPhoneFacade::getEffectiveUserInfo(char* name, size_t name_size, char* short_name, size_t short_size) const
{
    platform::shared::ble_bridge::copyBounded(name, name_size, services_.config().node_name);
    platform::shared::ble_bridge::copyBounded(short_name, short_size, services_.config().short_name);
}
chat::NodeId LinuxPhoneFacade::getSelfNodeId() const { return services_.selfNodeId(); }
bool LinuxPhoneFacade::sendPhoneText(chat::ChannelId channel, const std::string& text, chat::MessageId forced, chat::NodeId peer, chat::MessageId& out)
{
    out = services_.chat().sendTextWithId(channel, text, forced, peer);
    return out != 0;
}
bool LinuxPhoneFacade::sendPhoneAppData(chat::ChannelId channel, uint32_t port, const uint8_t* payload, size_t size, chat::NodeId dest, bool ack, chat::MessageId id, bool response)
{
    auto* adapter = services_.meshAdapter();
    return adapter && adapter->sendAppData(channel, port, payload, size, dest, ack, id, response);
}
bool LinuxPhoneFacade::pollIncomingPhoneData(chat::MeshIncomingData& out)
{
    if (incoming_.empty()) return false;
    out = std::move(incoming_.front());
    incoming_.pop_front();
    return true;
}
size_t LinuxPhoneFacade::phoneNodeCount() const
{
    const auto entries = services_.contacts().getAllPeers();
    return std::count_if(entries.begin(), entries.end(), [&](const auto& entry)
                         { return entry.protocol == nodeProtocol(services_.meshProtocol()); });
}
bool LinuxPhoneFacade::getPhoneNodeByIndex(size_t index, phone::PhoneNodeView& out) const
{
    for (const auto& entry : services_.contacts().getAllPeers())
    {
        if (entry.protocol != nodeProtocol(services_.meshProtocol())) continue;
        if (index-- == 0)
        {
            nodeView(entry, out);
            return true;
        }
    }
    out = {};
    return false;
}
bool LinuxPhoneFacade::findPhoneNode(chat::NodeId id, phone::PhoneNodeView& out) const
{
    for (const auto& entry : services_.contacts().getAllPeers())
        if (entry.protocol == nodeProtocol(services_.meshProtocol()) && entry.node_id == id)
        {
            nodeView(entry, out);
            return true;
        }
    out = {};
    return false;
}
bool LinuxPhoneFacade::isMeshPkiReady() const
{
    const auto* adapter = services_.meshAdapter();
    return adapter && adapter->isPkiReady();
}
bool LinuxPhoneFacade::isBleEnabled() const { return services_.isBleEnabled(); }
void LinuxPhoneFacade::setBleEnabled(bool enabled) { services_.setBleEnabled(enabled); }
bool LinuxPhoneFacade::getDeviceMacAddress(uint8_t out[6]) const
{
    if (!out) return false;
    std::fill_n(out, 6, 0);
    if (!has_mac_) return false;
    std::copy(mac_.begin(), mac_.end(), out);
    return true;
}
bool LinuxPhoneFacade::syncCurrentEpochSeconds(uint32_t epoch)
{
    return epoch != 0 && uint64_t(epoch) <= uint64_t(std::numeric_limits<time_t>::max()) && platform::ui::time::set_utc_time(static_cast<time_t>(epoch));
}
void LinuxPhoneFacade::resetMeshConfig()
{
    auto& config = services_.config();
    if (services_.meshProtocol() == chat::MeshProtocol::Meshtastic) config.meshtastic_config = chat::MeshConfig{};
    else if (services_.meshProtocol() == chat::MeshProtocol::MeshCore) config.meshcore_config = chat::MeshConfig{};
    else return;
    saveConfig();
    applyMeshConfig();
}
void LinuxPhoneFacade::clearNodeDb() { services_.clearNodeDb(); }
void LinuxPhoneFacade::clearMessageDb() { services_.clearMessageDb(); }
void LinuxPhoneFacade::restartDevice() { services_.restartDevice(); }
chat::meshcore::IMeshCoreBleBackend* LinuxPhoneFacade::meshCoreBackend() const
{
    auto* adapter = services_.meshAdapter();
    if (!adapter || services_.meshProtocol() != chat::MeshProtocol::MeshCore) return nullptr;
    auto* backend = adapter->backendForProtocol(chat::MeshProtocol::MeshCore);
    return backend ? backend->asMeshCoreBleBackend() : adapter->asMeshCoreBleBackend();
}
bool LinuxPhoneFacade::meshCoreExportIdentityPublicKey(uint8_t* out, size_t size)
{
    auto* backend = meshCoreBackend();
    return backend && out && size >= chat::meshcore::kMeshCorePubKeySize && backend->exportIdentityPublicKey(out);
}
bool LinuxPhoneFacade::meshCoreExportIdentityPrivateKey(uint8_t* out, size_t size)
{
    auto* backend = meshCoreBackend();
    return backend && out && size >= chat::meshcore::kMeshCorePrivKeySize && backend->exportIdentityPrivateKey(out);
}
bool LinuxPhoneFacade::meshCoreImportIdentityPrivateKey(const uint8_t* data, size_t size)
{
    auto* backend = meshCoreBackend();
    return backend && backend->importIdentityPrivateKey(data, size);
}
bool LinuxPhoneFacade::meshCoreSignPayload(const uint8_t* data, size_t size, uint8_t* signature, size_t capacity)
{
    auto* backend = meshCoreBackend();
    return backend && signature && capacity >= chat::meshcore::kMeshCoreSignatureSize && backend->signPayload(data, size, signature);
}
bool LinuxPhoneFacade::meshCoreSendSelfAdvert(bool broadcast)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendSelfAdvert(broadcast);
}
bool LinuxPhoneFacade::meshCoreSendPeerRequestType(const uint8_t* key, size_t size, uint8_t type, uint32_t* tag, uint32_t* timeout, bool* flood)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendPeerRequestType(key, size, type, tag, timeout, flood);
}
bool LinuxPhoneFacade::meshCoreSendPeerRequestPayload(const uint8_t* key, size_t key_size, const uint8_t* payload, size_t size, bool force, uint32_t* tag, uint32_t* timeout, bool* flood)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendPeerRequestPayload(key, key_size, payload, size, force, tag, timeout, flood);
}
bool LinuxPhoneFacade::meshCoreSendAnonRequestPayload(const uint8_t* key, size_t key_size, const uint8_t* payload, size_t size, uint32_t* timeout, bool* flood)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendAnonRequestPayload(key, key_size, payload, size, timeout, flood);
}
bool LinuxPhoneFacade::meshCoreSendTracePath(const uint8_t* path, size_t size, uint32_t tag, uint32_t auth, uint8_t flags, uint32_t* timeout)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendTracePath(path, size, tag, auth, flags, timeout);
}
bool LinuxPhoneFacade::meshCoreSendControlData(const uint8_t* payload, size_t size)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendControlData(payload, size);
}
bool LinuxPhoneFacade::meshCoreSendRawData(const uint8_t* path, size_t path_size, const uint8_t* payload, size_t size, uint32_t* timeout)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendRawData(path, path_size, payload, size, timeout);
}
bool LinuxPhoneFacade::meshCoreSendRawDataEx(uint8_t profile, const uint8_t* path, size_t path_size, const uint8_t* payload, size_t size, uint32_t* timeout)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendRawDataEx(profile, path, path_size, payload, size, timeout);
}
void LinuxPhoneFacade::meshCoreSetFloodScopeKey(const uint8_t* key, size_t size)
{
    if (auto* backend = meshCoreBackend()) backend->setFloodScopeKey(key, size);
}
} // namespace trailmate::linux_app
