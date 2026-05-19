#include "ble/app_phone_facade.h"

#include "board/BoardBase.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "chat/infra/meshcore/meshcore_ble_backend.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "platform/shared/ble/app_config_phone_snapshot_bridge.h"
#include "platform/shared/ble/meshtastic_phone_runtime_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ble
{
namespace
{
int estimateBatteryMv(int percent)
{
    if (percent < 0)
    {
        return 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    return 3300 + (percent * 9);
}

void copyBounded(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

bool parseBoolValue(const char* value, bool* out)
{
    if (!value || !out)
    {
        return false;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "on") == 0 || std::strcmp(value, "yes") == 0)
    {
        *out = true;
        return true;
    }
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "off") == 0 || std::strcmp(value, "no") == 0)
    {
        *out = false;
        return true;
    }
    return false;
}

void appendCustomVar(std::string& out, const char* key, const char* value)
{
    if (!key || !value)
    {
        return;
    }
    if (!out.empty())
    {
        out.push_back(',');
    }
    out += key;
    out.push_back(':');
    out += value;
}

chat::meshtastic::MtAdapter* getMeshtasticBackend(app::IAppBleFacade& app)
{
    auto* adapter = app.getMeshAdapter();
    if (!adapter || adapter->backendProtocol() != chat::MeshProtocol::Meshtastic)
    {
        return nullptr;
    }
    auto* backend = adapter->backendForProtocol(adapter->backendProtocol());
    return static_cast<chat::meshtastic::MtAdapter*>(backend);
}

void copyNodeView(const chat::contacts::NodeEntry& entry, phone::PhoneNodeView& out)
{
    out = {};
    out.node_id = entry.node_id;
    copyBounded(out.short_name, sizeof(out.short_name), entry.short_name);
    copyBounded(out.long_name, sizeof(out.long_name), entry.long_name);
    out.last_seen = entry.last_seen;
    out.snr = entry.snr;
    out.rssi = entry.rssi;
    out.hops_away = entry.hops_away;
    out.channel = entry.channel;
    out.next_hop = entry.next_hop;
    out.protocol = entry.protocol;
    out.role = entry.role;
    out.hw_model = entry.hw_model;
    out.has_macaddr = entry.has_macaddr;
    std::memcpy(out.macaddr, entry.macaddr, sizeof(out.macaddr));
    out.via_mqtt = entry.via_mqtt;
    out.is_ignored = entry.is_ignored;
    out.has_public_key = entry.has_public_key;
    out.key_manually_verified = entry.key_manually_verified;
    out.has_device_metrics = entry.has_device_metrics;
    out.device_metrics = entry.device_metrics;
    out.position.valid = entry.position_valid;
    out.position.latitude_i = entry.position_latitude_i;
    out.position.longitude_i = entry.position_longitude_i;
    out.position.has_altitude = entry.position_has_altitude;
    out.position.altitude = entry.position_altitude;
    out.position.timestamp = entry.position_timestamp;
    out.position.precision_bits = entry.position_precision_bits;
    out.position.pdop = entry.position_pdop;
    out.position.hdop = entry.position_hdop;
    out.position.vdop = entry.position_vdop;
    out.position.gps_accuracy_mm = entry.position_gps_accuracy_mm;
}
} // namespace

AppPhoneFacade::AppPhoneFacade(app::IAppBleFacade& app,
                               meshtastic_Config_BluetoothConfig& bluetooth_config,
                               meshtastic_LocalModuleConfig& module_config,
                               platform::shared::ble_bridge::IPhoneBleRuntime* ble_runtime)
    : app_(app),
      bluetooth_config_(bluetooth_config),
      module_config_(module_config),
      ble_runtime_(ble_runtime)
{
}

void AppPhoneFacade::setBleRuntime(platform::shared::ble_bridge::IPhoneBleRuntime* runtime)
{
    ble_runtime_ = runtime;
}

void AppPhoneFacade::syncMeshtasticMqttProxySettings(const meshtastic_LocalModuleConfig& module_config)
{
    auto* mt = getMeshtasticBackend(app_);
    if (!mt)
    {
        return;
    }

    const auto cfg = getMeshtasticPhoneConfig();
    chat::meshtastic::MtAdapter::MqttProxySettings settings;
    platform::shared::ble_bridge::applyMeshtasticMqttProxySettings(settings, module_config, cfg);
    mt->setMqttProxySettings(settings);
}

phone::MeshtasticPhoneConfigSnapshot AppPhoneFacade::getMeshtasticPhoneConfig() const
{
    return platform::shared::ble_bridge::makeMeshtasticPhoneConfigSnapshot(app_.getConfig());
}

void AppPhoneFacade::setMeshtasticPhoneConfig(const phone::MeshtasticPhoneConfigSnapshot& config)
{
    platform::shared::ble_bridge::applyMeshtasticPhoneConfigSnapshot(app_.getConfig(), config);
}

phone::MeshCorePhoneConfigSnapshot AppPhoneFacade::getMeshCorePhoneConfig() const
{
    return platform::shared::ble_bridge::makeMeshCorePhoneConfigSnapshot(app_.getConfig());
}

void AppPhoneFacade::setMeshCorePhoneConfig(const phone::MeshCorePhoneConfigSnapshot& config)
{
    platform::shared::ble_bridge::applyMeshCorePhoneConfigSnapshot(app_.getConfig(), config);
}

void AppPhoneFacade::saveConfig()
{
    app_.saveConfig();
}

void AppPhoneFacade::applyMeshConfig()
{
    app_.applyMeshConfig();
}

void AppPhoneFacade::applyUserInfo()
{
    app_.applyUserInfo();
}

void AppPhoneFacade::applyPositionConfig()
{
    app_.applyPositionConfig();
}

void AppPhoneFacade::getEffectiveUserInfo(char* out_long,
                                          std::size_t long_len,
                                          char* out_short,
                                          std::size_t short_len) const
{
    app_.getEffectiveUserInfo(out_long, long_len, out_short, short_len);
}

chat::NodeId AppPhoneFacade::getSelfNodeId() const
{
    return app_.getSelfNodeId();
}

bool AppPhoneFacade::sendPhoneText(chat::ChannelId channel,
                                   const std::string& text,
                                   chat::MessageId forced_msg_id,
                                   chat::NodeId peer,
                                   chat::MessageId& out_msg_id)
{
    out_msg_id = app_.getChatService().sendTextWithId(channel, text, forced_msg_id, peer);
    return out_msg_id != 0;
}

bool AppPhoneFacade::sendPhoneAppData(chat::ChannelId channel,
                                      uint32_t portnum,
                                      const uint8_t* payload,
                                      std::size_t len,
                                      chat::NodeId dest,
                                      bool want_ack,
                                      chat::MessageId packet_id,
                                      bool want_response)
{
    auto* adapter = app_.getMeshAdapter();
    return adapter && adapter->sendAppData(channel, portnum, payload, len, dest, want_ack, packet_id, want_response);
}

bool AppPhoneFacade::pollIncomingPhoneData(chat::MeshIncomingData& out)
{
    auto* adapter = app_.getMeshAdapter();
    return adapter && adapter->pollIncomingData(&out);
}

std::size_t AppPhoneFacade::phoneNodeCount() const
{
    const auto* store = app_.getNodeStore();
    return store ? store->getEntries().size() : 0;
}

bool AppPhoneFacade::getPhoneNodeByIndex(std::size_t index, phone::PhoneNodeView& out) const
{
    const auto* store = app_.getNodeStore();
    if (!store)
    {
        return false;
    }
    const auto& entries = store->getEntries();
    if (index >= entries.size())
    {
        return false;
    }
    copyNodeView(entries[index], out);
    if (const auto* node = app_.getContactService().getNodeInfo(out.node_id))
    {
        out.position = node->position;
    }
    return true;
}

bool AppPhoneFacade::findPhoneNode(chat::NodeId node_id, phone::PhoneNodeView& out) const
{
    const auto* store = app_.getNodeStore();
    if (!store)
    {
        return false;
    }
    for (const auto& entry : store->getEntries())
    {
        if (entry.node_id == node_id)
        {
            copyNodeView(entry, out);
            if (const auto* node = app_.getContactService().getNodeInfo(out.node_id))
            {
                out.position = node->position;
            }
            return true;
        }
    }
    return false;
}

bool AppPhoneFacade::isMeshPkiReady() const
{
    const auto* adapter = app_.getMeshAdapter();
    return adapter ? adapter->isPkiReady() : false;
}

bool AppPhoneFacade::isBleEnabled() const
{
    return app_.isBleEnabled();
}

void AppPhoneFacade::setBleEnabled(bool enabled)
{
    app_.setBleEnabled(enabled);
}

bool AppPhoneFacade::getDeviceMacAddress(uint8_t out_mac[6]) const
{
    return app_.getDeviceMacAddress(out_mac);
}

bool AppPhoneFacade::syncCurrentEpochSeconds(uint32_t epoch_seconds)
{
    return app_.syncCurrentEpochSeconds(epoch_seconds);
}

void AppPhoneFacade::resetMeshConfig()
{
    app_.resetMeshConfig();
}

void AppPhoneFacade::clearNodeDb()
{
    app_.clearNodeDb();
}

void AppPhoneFacade::clearMessageDb()
{
    app_.clearMessageDb();
}

void AppPhoneFacade::restartDevice()
{
    app_.restartDevice();
}

bool AppPhoneFacade::loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const
{
    return meshtastic_config_bridge::loadBluetoothConfigView(bluetooth_config_, app_.isBleEnabled(), out);
}

void AppPhoneFacade::saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config)
{
    bluetooth_config_ = config;
    meshtastic_config_bridge::normalizeBluetoothConfig(&bluetooth_config_);
    if (ble_runtime_)
    {
        ble_runtime_->onPhoneBluetoothConfigChanged();
    }
}

bool AppPhoneFacade::loadModuleConfig(meshtastic_LocalModuleConfig* out) const
{
    if (!out)
    {
        return false;
    }
    *out = module_config_;
    meshtastic_config_bridge::normalizeModuleConfig(out);
    return true;
}

void AppPhoneFacade::saveModuleConfig(const meshtastic_LocalModuleConfig& config)
{
    module_config_ = config;
    meshtastic_config_bridge::normalizeModuleConfig(&module_config_);
    if (ble_runtime_)
    {
        ble_runtime_->onPhoneModuleConfigChanged();
    }
}

void AppPhoneFacade::onConfigStart()
{
    if (ble_runtime_)
    {
        ble_runtime_->requestPhoneHighThroughputConnection();
    }
}

void AppPhoneFacade::onConfigComplete()
{
    if (ble_runtime_)
    {
        ble_runtime_->requestPhoneLowerPowerConnection();
    }
}

bool AppPhoneFacade::loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const
{
    const uint32_t pending_passkey = ble_runtime_ ? ble_runtime_->pendingPhoneBlePasskey() : 0;
    const bool connected = ble_runtime_ ? ble_runtime_->isPhoneBleConnected() : false;
    const uint32_t passkey = meshtastic_config_bridge::resolveBlePasskey(bluetooth_config_, pending_passkey);
    return meshtastic_config_bridge::fillDeviceConnectionStatus(passkey, connected, out);
}

bool AppPhoneFacade::handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg)
{
    auto* mt = getMeshtasticBackend(app_);
    return mt ? mt->handleMqttProxyMessage(msg) : false;
}

bool AppPhoneFacade::pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out)
{
    auto* mt = getMeshtasticBackend(app_);
    return (mt && out) ? mt->pollMqttProxyMessage(out) : false;
}

bool AppPhoneFacade::loadTimezoneTzdef(char* out, size_t out_len) const
{
    return platform::shared::ble_bridge::loadTimezoneTzdefFromSettings(out, out_len);
}

void AppPhoneFacade::saveTimezoneTzdef(const char* tzdef)
{
    platform::shared::ble_bridge::saveTimezoneTzdefToSettings(tzdef);
}

int AppPhoneFacade::getTimezoneOffsetMinutes() const
{
    return platform::shared::ble_bridge::getTimezoneOffsetMinutes();
}

void AppPhoneFacade::setTimezoneOffsetMinutes(int offset_min)
{
    platform::shared::ble_bridge::setTimezoneOffsetMinutes(offset_min);
}

bool AppPhoneFacade::getGpsFix(phone::meshtastic::MeshtasticGpsFix* out) const
{
    return platform::shared::ble_bridge::fillMeshtasticGpsFixFromLocationSource(gps::gps_location_source(), out);
}

phone::meshcore::MeshCorePhoneBatteryInfo AppPhoneFacade::getBatteryInfo() const
{
    phone::meshcore::MeshCorePhoneBatteryInfo info{};
    int level = -1;
    if (BoardBase* board = app_.getBoard())
    {
        level = board->getBatteryLevel();
    }
    if (level < 0)
    {
        level = 0;
    }
    if (level > 100)
    {
        level = 100;
    }
    info.level = static_cast<uint8_t>(level);
    info.millivolts = static_cast<uint16_t>(estimateBatteryMv(level));
    return info;
}

bool AppPhoneFacade::getCustomVars(std::string* out) const
{
    if (!out)
    {
        return false;
    }
    out->clear();

    char buf[40];
    if (gps::gps_is_enabled())
    {
        appendCustomVar(*out, "gps", gps::gps_is_powered() ? "1" : "0");
    }
    const auto cfg = getMeshCorePhoneConfig();
    appendCustomVar(*out, "node_name", cfg.node_name);
    appendCustomVar(*out, "channel_name", cfg.mesh.meshcore_channel_name);
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(cfg.mesh.meshcore_multi_acks ? 1U : 0U));
    appendCustomVar(*out, "multi_acks", buf);
    return true;
}

bool AppPhoneFacade::setCustomVar(const char* key, const char* value)
{
    if (!key || !value)
    {
        return false;
    }

    auto mesh_cfg = getMeshCorePhoneConfig();
    auto mt_cfg = getMeshtasticPhoneConfig();
    bool changed = false;
    bool mesh_changed = false;
    bool mt_changed = false;
    bool apply_mesh = false;
    bool apply_user = false;
    bool apply_position = false;

    if (std::strcmp(key, "node_name") == 0)
    {
        copyBounded(mesh_cfg.node_name, sizeof(mesh_cfg.node_name), value);
        copyBounded(mt_cfg.node_name, sizeof(mt_cfg.node_name), value);
        changed = true;
        mesh_changed = true;
        mt_changed = true;
        apply_user = true;
    }
    else if (std::strcmp(key, "channel_name") == 0)
    {
        copyBounded(mesh_cfg.mesh.meshcore_channel_name, sizeof(mesh_cfg.mesh.meshcore_channel_name), value);
        changed = true;
        mesh_changed = true;
        apply_mesh = true;
    }
    else if (std::strcmp(key, "gps") == 0)
    {
        bool parsed = false;
        if (!parseBoolValue(value, &parsed))
        {
            return false;
        }
        mt_cfg.gps_enabled = parsed;
        changed = true;
        mt_changed = true;
        apply_position = true;
    }
    else if (std::strcmp(key, "multi_acks") == 0)
    {
        bool parsed = false;
        if (!parseBoolValue(value, &parsed))
        {
            return false;
        }
        mesh_cfg.mesh.meshcore_multi_acks = parsed;
        changed = true;
        mesh_changed = true;
        apply_mesh = true;
    }
    else
    {
        return false;
    }

    if (mesh_changed)
    {
        setMeshCorePhoneConfig(mesh_cfg);
    }
    if (mt_changed)
    {
        setMeshtasticPhoneConfig(mt_cfg);
    }
    if (changed)
    {
        app_.saveConfig();
    }
    if (apply_user)
    {
        app_.applyUserInfo();
    }
    if (apply_mesh)
    {
        app_.applyMeshConfig();
    }
    if (apply_position)
    {
        app_.applyPositionConfig();
    }
    return true;
}

bool AppPhoneFacade::setTuningParams(const phone::meshcore::MeshCorePhoneTuningParams& params)
{
    auto cfg = getMeshCorePhoneConfig();
    cfg.mesh.meshcore_rx_delay_base = static_cast<float>(params.rx_delay_base_ms) / 1000.0f;
    cfg.mesh.meshcore_airtime_factor = static_cast<float>(params.airtime_factor_milli) / 1000.0f;
    setMeshCorePhoneConfig(cfg);
    app_.saveConfig();
    app_.applyMeshConfig();
    return true;
}

bool AppPhoneFacade::getTuningParams(phone::meshcore::MeshCorePhoneTuningParams* out) const
{
    if (!out)
    {
        return false;
    }
    const auto cfg = getMeshCorePhoneConfig();
    out->rx_delay_base_ms = static_cast<uint32_t>(cfg.mesh.meshcore_rx_delay_base * 1000.0f);
    out->airtime_factor_milli = static_cast<uint32_t>(cfg.mesh.meshcore_airtime_factor * 1000.0f);
    return true;
}

chat::meshcore::IMeshCoreBleBackend* AppPhoneFacade::meshCoreBackend()
{
    auto* adapter = app_.getMeshAdapter();
    if (!adapter || app_.getConfig().mesh_protocol != chat::MeshProtocol::MeshCore)
    {
        return nullptr;
    }
    if (auto* backend = adapter->backendForProtocol(chat::MeshProtocol::MeshCore))
    {
        return backend->asMeshCoreBleBackend();
    }
    return adapter->asMeshCoreBleBackend();
}

const chat::meshcore::IMeshCoreBleBackend* AppPhoneFacade::meshCoreBackend() const
{
    auto* adapter = app_.getMeshAdapter();
    if (!adapter || app_.getConfig().mesh_protocol != chat::MeshProtocol::MeshCore)
    {
        return nullptr;
    }
    if (auto* backend = adapter->backendForProtocol(chat::MeshProtocol::MeshCore))
    {
        return backend->asMeshCoreBleBackend();
    }
    return adapter->asMeshCoreBleBackend();
}

bool AppPhoneFacade::meshCoreExportIdentityPublicKey(uint8_t* out_pubkey, std::size_t len)
{
    auto* backend = meshCoreBackend();
    return backend && out_pubkey && len >= chat::meshcore::kMeshCorePubKeySize &&
           backend->exportIdentityPublicKey(out_pubkey);
}

bool AppPhoneFacade::meshCoreExportIdentityPrivateKey(uint8_t* out_priv, std::size_t len)
{
    auto* backend = meshCoreBackend();
    return backend && out_priv && len >= chat::meshcore::kMeshCorePrivKeySize &&
           backend->exportIdentityPrivateKey(out_priv);
}

bool AppPhoneFacade::meshCoreImportIdentityPrivateKey(const uint8_t* in_priv, std::size_t len)
{
    auto* backend = meshCoreBackend();
    return backend && backend->importIdentityPrivateKey(in_priv, len);
}

bool AppPhoneFacade::meshCoreSignPayload(const uint8_t* data, std::size_t len, uint8_t* out_sig, std::size_t out_len)
{
    auto* backend = meshCoreBackend();
    return backend && out_sig && out_len >= chat::meshcore::kMeshCoreSignatureSize &&
           backend->signPayload(data, len, out_sig);
}

bool AppPhoneFacade::meshCoreSendSelfAdvert(bool broadcast)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendSelfAdvert(broadcast);
}

bool AppPhoneFacade::meshCoreSendPeerRequestType(const uint8_t* pubkey,
                                                 std::size_t len,
                                                 uint8_t req_type,
                                                 uint32_t* out_tag,
                                                 uint32_t* out_est_timeout,
                                                 bool* out_sent_flood)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendPeerRequestType(pubkey, len, req_type, out_tag, out_est_timeout, out_sent_flood);
}

bool AppPhoneFacade::meshCoreSendPeerRequestPayload(const uint8_t* pubkey,
                                                    std::size_t len,
                                                    const uint8_t* payload,
                                                    std::size_t payload_len,
                                                    bool force_flood,
                                                    uint32_t* out_tag,
                                                    uint32_t* out_est_timeout,
                                                    bool* out_sent_flood)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendPeerRequestPayload(pubkey,
                                                      len,
                                                      payload,
                                                      payload_len,
                                                      force_flood,
                                                      out_tag,
                                                      out_est_timeout,
                                                      out_sent_flood);
}

bool AppPhoneFacade::meshCoreSendAnonRequestPayload(const uint8_t* pubkey,
                                                    std::size_t len,
                                                    const uint8_t* payload,
                                                    std::size_t payload_len,
                                                    uint32_t* out_est_timeout,
                                                    bool* out_sent_flood)
{
    auto* backend = meshCoreBackend();
    return backend &&
           backend->sendAnonRequestPayload(pubkey, len, payload, payload_len, out_est_timeout, out_sent_flood);
}

bool AppPhoneFacade::meshCoreSendTracePath(const uint8_t* path,
                                           std::size_t path_len,
                                           uint32_t tag,
                                           uint32_t auth,
                                           uint8_t flags,
                                           uint32_t* out_est_timeout)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendTracePath(path, path_len, tag, auth, flags, out_est_timeout);
}

bool AppPhoneFacade::meshCoreSendControlData(const uint8_t* payload, std::size_t payload_len)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendControlData(payload, payload_len);
}

bool AppPhoneFacade::meshCoreSendRawData(const uint8_t* path,
                                         std::size_t path_len,
                                         const uint8_t* payload,
                                         std::size_t payload_len,
                                         uint32_t* out_est_timeout)
{
    auto* backend = meshCoreBackend();
    return backend && backend->sendRawData(path, path_len, payload, payload_len, out_est_timeout);
}

void AppPhoneFacade::meshCoreSetFloodScopeKey(const uint8_t* key, std::size_t len)
{
    if (auto* backend = meshCoreBackend())
    {
        backend->setFloodScopeKey(key, len);
    }
}

} // namespace ble
