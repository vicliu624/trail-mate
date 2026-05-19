#include "../../include/ble/app_phone_facade.h"

#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "chat/infra/meshcore/meshcore_ble_backend.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_node_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"
#include "platform/shared/ble/app_config_phone_snapshot_bridge.h"
#include "platform/shared/ble/meshtastic_phone_runtime_bridge.h"

#include <cstring>

namespace ble
{
namespace
{
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

platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter* getMeshtasticBackend(
    app::IAppBleFacade& app)
{
    auto* adapter = app.getMeshAdapter();
    if (!adapter || adapter->backendProtocol() != chat::MeshProtocol::Meshtastic)
    {
        return nullptr;
    }

    auto* backend = adapter->backendForProtocol(chat::MeshProtocol::Meshtastic);
    return static_cast<platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter*>(backend);
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
    platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter::MqttProxySettings settings;
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
    bluetooth_config_.enabled = config.enabled;
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
    const bool connected = ble_runtime_ ? ble_runtime_->isPhoneBleConnected() : false;
    const uint32_t passkey = ble_runtime_ ? ble_runtime_->pendingPhoneBlePasskey() : 0;
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
    if (!out || out_len == 0)
    {
        return false;
    }

    out[0] = '\0';

#if defined(GAT562_MESH_EVB_PRO)
    return false;
#else
    return platform::shared::ble_bridge::loadTimezoneTzdefFromSettings(out, out_len);
#endif
}

void AppPhoneFacade::saveTimezoneTzdef(const char* tzdef)
{
#if defined(GAT562_MESH_EVB_PRO)
    (void)tzdef;
#else
    platform::shared::ble_bridge::saveTimezoneTzdefToSettings(tzdef);
#endif
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
    return platform::shared::ble_bridge::setMeshtasticGpsFixUnavailable(out);
}

bool AppPhoneFacade::getCustomVars(std::string* out) const
{
    if (!out)
    {
        return false;
    }

    out->clear();

    const auto mesh_cfg = getMeshCorePhoneConfig();
    const auto mt_cfg = getMeshtasticPhoneConfig();
    appendCustomVar(*out, "node_name", mesh_cfg.node_name);
    appendCustomVar(*out, "channel_name", mesh_cfg.mesh.meshcore_channel_name);
    appendCustomVar(*out, "multi_acks", mesh_cfg.mesh.meshcore_multi_acks ? "1" : "0");
    appendCustomVar(*out, "gps", mt_cfg.gps_enabled ? "1" : "0");

    appendCustomVar(*out, "manual_add_contacts", "0");
    appendCustomVar(*out, "telemetry_base", "0");
    appendCustomVar(*out, "telemetry_loc", "0");
    appendCustomVar(*out, "telemetry_env", "0");
    appendCustomVar(*out, "advert_loc_policy", "0");
    appendCustomVar(*out, "ble_pin", "0");

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
    bool position_changed = false;

    if (std::strcmp(key, "node_name") == 0)
    {
        char next[sizeof(mesh_cfg.node_name)] = {};
        copyBounded(next, sizeof(next), value);
        if (std::strcmp(mesh_cfg.node_name, next) != 0)
        {
            copyBounded(mesh_cfg.node_name, sizeof(mesh_cfg.node_name), next);
            copyBounded(mt_cfg.node_name, sizeof(mt_cfg.node_name), next);
            changed = true;
            mesh_changed = true;
            mt_changed = true;
        }
    }
    else if (std::strcmp(key, "channel_name") == 0)
    {
        char next[sizeof(mesh_cfg.mesh.meshcore_channel_name)] = {};
        copyBounded(next, sizeof(next), value);
        if (std::strcmp(mesh_cfg.mesh.meshcore_channel_name, next) != 0)
        {
            copyBounded(mesh_cfg.mesh.meshcore_channel_name, sizeof(mesh_cfg.mesh.meshcore_channel_name), next);
            changed = true;
            mesh_changed = true;
        }
    }
    else if (std::strcmp(key, "multi_acks") == 0)
    {
        bool parsed = false;
        if (!parseBoolValue(value, &parsed))
        {
            return false;
        }
        if (mesh_cfg.mesh.meshcore_multi_acks != parsed)
        {
            mesh_cfg.mesh.meshcore_multi_acks = parsed;
            changed = true;
            mesh_changed = true;
        }
    }
    else if (std::strcmp(key, "gps") == 0)
    {
        bool parsed = false;
        if (!parseBoolValue(value, &parsed))
        {
            return false;
        }
        if (mt_cfg.gps_enabled != parsed)
        {
            mt_cfg.gps_enabled = parsed;
            changed = true;
            mt_changed = true;
            position_changed = true;
        }
    }
    else if (std::strcmp(key, "manual_add_contacts") == 0 ||
             std::strcmp(key, "telemetry_base") == 0 ||
             std::strcmp(key, "telemetry_loc") == 0 ||
             std::strcmp(key, "telemetry_env") == 0 ||
             std::strcmp(key, "advert_loc_policy") == 0 ||
             std::strcmp(key, "ble_pin") == 0)
    {
        return true;
    }
    else
    {
        return false;
    }

    if (changed)
    {
        if (mesh_changed)
        {
            setMeshCorePhoneConfig(mesh_cfg);
        }
        if (mt_changed)
        {
            setMeshtasticPhoneConfig(mt_cfg);
        }
        app_.saveConfig();
    }
    if (position_changed)
    {
        app_.applyPositionConfig();
    }
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
