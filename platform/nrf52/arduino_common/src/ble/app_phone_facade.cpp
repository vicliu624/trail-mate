#include "../../include/ble/app_phone_facade.h"

#include "chat/ble/meshtastic_phone_config_bridge.h"
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

chat::ChatService& AppPhoneFacade::getChatService()
{
    return app_.getChatService();
}

chat::contacts::ContactService& AppPhoneFacade::getContactService()
{
    return app_.getContactService();
}

chat::IMeshAdapter* AppPhoneFacade::getMeshAdapter()
{
    return app_.getMeshAdapter();
}

const chat::IMeshAdapter* AppPhoneFacade::getMeshAdapter() const
{
    return app_.getMeshAdapter();
}

chat::contacts::INodeStore* AppPhoneFacade::getNodeStore()
{
    return app_.getNodeStore();
}

const chat::contacts::INodeStore* AppPhoneFacade::getNodeStore() const
{
    return app_.getNodeStore();
}

chat::NodeId AppPhoneFacade::getSelfNodeId() const
{
    return app_.getSelfNodeId();
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

} // namespace ble
