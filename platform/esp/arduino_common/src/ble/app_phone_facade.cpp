#include "ble/app_phone_facade.h"

#include "board/BoardBase.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
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
    return platform::shared::ble_bridge::fillMeshtasticGpsFixFromUiRuntime(out);
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

} // namespace ble
