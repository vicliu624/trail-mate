#include "ble/meshtastic_ble.h"

#include "app/app_config.h"
#include "board/BoardBase.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

namespace ble
{
namespace
{
constexpr const char* kBlePrefsNs = "mt_ble";
constexpr const char* kBleEnabledKey = "enabled";
constexpr const char* kBleModeKey = "mode";
constexpr const char* kBlePinKey = "pin";

constexpr const char* kModulePrefsNs = "mt_mod";
constexpr const char* kModuleBlobKey = "cfg";
constexpr uint32_t kModuleConfigVersion = 1;

constexpr const char* kDefaultMqttAddress = "mqtt.meshtastic.org";
constexpr const char* kDefaultMqttUsername = "meshdev";
constexpr const char* kDefaultMqttPassword = "large4cats";
constexpr const char* kDefaultMqttRoot = "msh";
constexpr bool kDefaultMqttEncryptionEnabled = true;
constexpr bool kDefaultMqttTlsEnabled = false;
constexpr uint32_t kDefaultMapPublishIntervalSecs = 60 * 60;
constexpr uint32_t kDefaultDetectionMinBroadcastSecs = 45;
constexpr uint32_t kDefaultAmbientCurrent = 10;

bool isValidBlePin(uint32_t pin)
{
    return pin == 0 || (pin >= 100000 && pin <= 999999);
}

template <typename T>
void zeroInit(T& value)
{
    std::memset(&value, 0, sizeof(value));
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

chat::meshtastic::MtAdapter* getMeshtasticBackend(app::IAppBleFacade& ctx)
{
    auto* adapter = ctx.getMeshAdapter();
    if (!adapter || adapter->backendProtocol() != chat::MeshProtocol::Meshtastic)
    {
        return nullptr;
    }
    auto* backend = adapter->backendForProtocol(adapter->backendProtocol());
    return static_cast<chat::meshtastic::MtAdapter*>(backend);
}

std::string channelDisplayName(app::IAppBleFacade& ctx, uint8_t idx)
{
    const auto& cfg = ctx.getConfig();
    if (idx == 1)
    {
        return "Secondary";
    }
    auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset);
    return cfg.meshtastic_config.use_preset
               ? std::string(chat::meshtastic::presetDisplayName(preset))
               : std::string("Custom");
}
} // namespace

bool MeshtasticBleService::isBleConnected() const
{
    return connected_;
}

bool MeshtasticBleService::loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const
{
    if (!out)
    {
        return false;
    }
    *out = ble_config_;
    return true;
}

void MeshtasticBleService::saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config)
{
    ble_config_ = config;
    saveBleConfig();
}

bool MeshtasticBleService::loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const
{
    if (!out)
    {
        return false;
    }
    meshtastic_DeviceConnectionStatus status = meshtastic_DeviceConnectionStatus_init_zero;
    *out = status;
    out->has_bluetooth = true;
    out->bluetooth.pin = (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN)
                             ? ble_config_.fixed_pin
                         : (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
                             ? 0
                             : pending_passkey_.load();
    out->bluetooth.rssi = 0;
    out->bluetooth.is_connected = connected_;
    return true;
}

bool MeshtasticBleService::loadModuleConfig(meshtastic_LocalModuleConfig* out) const
{
    if (!out)
    {
        return false;
    }
    *out = module_config_;
    return true;
}

void MeshtasticBleService::saveModuleConfig(const meshtastic_LocalModuleConfig& config)
{
    module_config_ = config;
    saveModuleConfig();
}

bool MeshtasticBleService::handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg)
{
    auto* mt = getMeshtasticBackend(ctx_);
    return mt ? mt->handleMqttProxyMessage(msg) : false;
}

bool MeshtasticBleService::pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out)
{
    auto* mt = getMeshtasticBackend(ctx_);
    return (mt && out) ? mt->pollMqttProxyMessage(out) : false;
}

void MeshtasticBleService::onConfigStart()
{
    requestHighThroughputConnection();
}

void MeshtasticBleService::onConfigComplete()
{
    requestLowerPowerConnection();
}

void MeshtasticBleService::loadBleConfig()
{
    ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    ble_config_.enabled = true;
    ble_config_.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    ble_config_.fixed_pin = 0;

    Preferences prefs;
    if (prefs.begin(kBlePrefsNs, true))
    {
        ble_config_.enabled = prefs.getBool(kBleEnabledKey, true);
        if (prefs.isKey(kBleModeKey))
        {
            uint8_t mode = prefs.getUChar(kBleModeKey,
                                          static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN));
            if (mode <= static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN))
            {
                ble_config_.mode = static_cast<meshtastic_Config_BluetoothConfig_PairingMode>(mode);
            }
        }
        uint32_t pin = prefs.getUInt(kBlePinKey, 0);
        if (isValidBlePin(pin))
        {
            ble_config_.fixed_pin = pin;
        }
        prefs.end();
    }
    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        ble_config_.fixed_pin = 0;
    }
}

void MeshtasticBleService::saveBleConfig()
{
    Preferences prefs;
    if (prefs.begin(kBlePrefsNs, false))
    {
        prefs.putBool(kBleEnabledKey, ble_config_.enabled);
        prefs.putUChar(kBleModeKey, static_cast<uint8_t>(ble_config_.mode));
        prefs.putUInt(kBlePinKey, ble_config_.fixed_pin);
        prefs.end();
    }
}

void MeshtasticBleService::applyBleSecurity()
{
    if (!ble_config_.enabled)
    {
        return;
    }

    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        return;
    }

    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM | BLE_SM_PAIR_AUTHREQ_SC);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

    if (ble_config_.fixed_pin != 0)
    {
        NimBLEDevice::setSecurityPasskey(ble_config_.fixed_pin);
    }
}

void MeshtasticBleService::loadModuleConfig()
{
    auto init_defaults = [&]()
    {
        zeroInit(module_config_);
        module_config_.version = kModuleConfigVersion;
        module_config_.has_mqtt = true;
        module_config_.has_serial = true;
        module_config_.has_external_notification = true;
        module_config_.has_store_forward = true;
        module_config_.has_range_test = true;
        module_config_.has_telemetry = true;
        module_config_.has_canned_message = true;
        module_config_.has_audio = true;
        module_config_.has_remote_hardware = true;
        module_config_.has_neighbor_info = true;
        module_config_.has_ambient_lighting = true;
        module_config_.has_detection_sensor = true;
        module_config_.has_paxcounter = true;

        copyBounded(module_config_.mqtt.address, sizeof(module_config_.mqtt.address), kDefaultMqttAddress);
        copyBounded(module_config_.mqtt.username, sizeof(module_config_.mqtt.username), kDefaultMqttUsername);
        copyBounded(module_config_.mqtt.password, sizeof(module_config_.mqtt.password), kDefaultMqttPassword);
        copyBounded(module_config_.mqtt.root, sizeof(module_config_.mqtt.root), kDefaultMqttRoot);
        module_config_.mqtt.encryption_enabled = kDefaultMqttEncryptionEnabled;
        module_config_.mqtt.tls_enabled = kDefaultMqttTlsEnabled;
        module_config_.mqtt.has_map_report_settings = true;
        module_config_.mqtt.map_report_settings.publish_interval_secs = kDefaultMapPublishIntervalSecs;
        module_config_.mqtt.map_report_settings.position_precision = 0;
        module_config_.mqtt.map_report_settings.should_report_location = false;

        module_config_.neighbor_info.enabled = false;
        module_config_.neighbor_info.update_interval = 0;
        module_config_.neighbor_info.transmit_over_lora = false;

        module_config_.telemetry.device_update_interval = 3600;
        module_config_.telemetry.device_telemetry_enabled = true;
        module_config_.telemetry.environment_update_interval = 0;
        module_config_.telemetry.environment_measurement_enabled = false;
        module_config_.telemetry.power_update_interval = 0;
        module_config_.telemetry.health_update_interval = 0;
        module_config_.telemetry.air_quality_interval = 0;

        module_config_.detection_sensor.enabled = false;
        module_config_.detection_sensor.detection_trigger_type =
            meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH;
        module_config_.detection_sensor.minimum_broadcast_secs = kDefaultDetectionMinBroadcastSecs;

        module_config_.ambient_lighting.current = kDefaultAmbientCurrent;
        uint32_t node_id = ctx_.getSelfNodeId();
        module_config_.ambient_lighting.red = (node_id >> 16) & 0xFF;
        module_config_.ambient_lighting.green = (node_id >> 8) & 0xFF;
        module_config_.ambient_lighting.blue = node_id & 0xFF;

#if defined(ROTARY_A) && defined(ROTARY_B) && defined(ROTARY_C)
        module_config_.canned_message.updown1_enabled = true;
        module_config_.canned_message.inputbroker_pin_a = ROTARY_A;
        module_config_.canned_message.inputbroker_pin_b = ROTARY_B;
        module_config_.canned_message.inputbroker_pin_press = ROTARY_C;
        module_config_.canned_message.inputbroker_event_cw =
            meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(28);
        module_config_.canned_message.inputbroker_event_ccw =
            meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(29);
        module_config_.canned_message.inputbroker_event_press =
            meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
#endif
    };

    init_defaults();

    Preferences prefs;
    if (prefs.begin(kModulePrefsNs, true))
    {
        size_t len = prefs.getBytes(kModuleBlobKey, &module_config_, sizeof(module_config_));
        prefs.end();
        if (len != sizeof(module_config_) || module_config_.version != kModuleConfigVersion)
        {
            init_defaults();
        }
    }
}

void MeshtasticBleService::saveModuleConfig()
{
    Preferences prefs;
    if (prefs.begin(kModulePrefsNs, false))
    {
        prefs.putBytes(kModuleBlobKey, &module_config_, sizeof(module_config_));
        prefs.end();
    }
}

void MeshtasticBleService::refreshBatteryLevel(bool notify)
{
    if (!battery_level_)
    {
        return;
    }

    int level = -1;
    if (BoardBase* board = ctx_.getBoard())
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
    if (last_battery_level_ == level)
    {
        return;
    }

    const uint8_t value = static_cast<uint8_t>(level);
    battery_level_->setValue(&value, 1);
    if (notify && connected_)
    {
        battery_level_->notify(&value, 1, BLE_HS_CONN_HANDLE_NONE);
    }
    last_battery_level_ = level;
}

void MeshtasticBleService::syncMqttProxySettings()
{
    auto* mt = getMeshtasticBackend(ctx_);
    if (!mt)
    {
        return;
    }

    const auto& cfg = ctx_.getConfig();
    chat::meshtastic::MtAdapter::MqttProxySettings settings;
    settings.enabled = module_config_.has_mqtt && module_config_.mqtt.enabled;
    settings.proxy_to_client_enabled = module_config_.has_mqtt && module_config_.mqtt.proxy_to_client_enabled;
    settings.encryption_enabled = module_config_.has_mqtt ? module_config_.mqtt.encryption_enabled : true;
    settings.primary_uplink_enabled = cfg.primary_uplink_enabled;
    settings.primary_downlink_enabled = cfg.primary_downlink_enabled;
    settings.secondary_uplink_enabled = cfg.secondary_uplink_enabled;
    settings.secondary_downlink_enabled = cfg.secondary_downlink_enabled;
    settings.root = module_config_.mqtt.root[0] ? module_config_.mqtt.root : kDefaultMqttRoot;
    settings.primary_channel_id = channelDisplayName(ctx_, 0);
    settings.secondary_channel_id = channelDisplayName(ctx_, 1);
    mt->setMqttProxySettings(settings);
}

} // namespace ble
