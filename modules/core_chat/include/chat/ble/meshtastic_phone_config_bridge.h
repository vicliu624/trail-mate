#pragma once

#include "chat/ble/meshtastic_defaults.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/localonly.pb.h"
#include "meshtastic/mesh.pb.h"

#include <cstdint>
#include <cstring>

namespace ble::meshtastic_config_bridge
{

struct PersistedState
{
    bool has_bluetooth = false;
    bool has_module = false;
    meshtastic_Config_BluetoothConfig bluetooth = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module = meshtastic_LocalModuleConfig_init_zero;
};

inline bool isValidBluetoothMode(uint8_t mode)
{
    return mode <= static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN);
}

inline bool isValidBlePin(uint32_t pin)
{
    return pin == 0 || (pin >= 100000U && pin <= 999999U);
}

inline void copyBounded(char* dst, size_t dst_len, const char* src)
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

inline void normalizeBluetoothConfig(meshtastic_Config_BluetoothConfig* config)
{
    if (!config)
    {
        return;
    }
    if (!isValidBluetoothMode(static_cast<uint8_t>(config->mode)))
    {
        config->mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    }
    if (!isValidBlePin(config->fixed_pin) ||
        config->mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        config->fixed_pin = 0;
    }
}

inline bool loadBluetoothConfigView(const meshtastic_Config_BluetoothConfig& stored,
                                    bool runtime_enabled,
                                    meshtastic_Config_BluetoothConfig* out)
{
    if (!out)
    {
        return false;
    }
    *out = stored;
    normalizeBluetoothConfig(out);
    out->enabled = runtime_enabled;
    return true;
}

inline uint32_t resolveBlePasskey(const meshtastic_Config_BluetoothConfig& config, uint32_t pending_passkey)
{
    if (pending_passkey != 0)
    {
        return pending_passkey;
    }
    if (config.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN)
    {
        return config.fixed_pin;
    }
    return 0;
}

inline bool fillDeviceConnectionStatus(uint32_t passkey,
                                       bool is_connected,
                                       meshtastic_DeviceConnectionStatus* out)
{
    if (!out)
    {
        return false;
    }

    meshtastic_DeviceConnectionStatus status = meshtastic_DeviceConnectionStatus_init_zero;
    *out = status;
    out->has_bluetooth = true;
    out->bluetooth.pin = passkey;
    out->bluetooth.rssi = 0;
    out->bluetooth.is_connected = is_connected;
    return true;
}

inline void repairLegacyMqttConfig(meshtastic_LocalModuleConfig* config)
{
    if (!config)
    {
        return;
    }

    config->has_mqtt = true;
    config->mqtt.address[sizeof(config->mqtt.address) - 1] = '\0';
    config->mqtt.username[sizeof(config->mqtt.username) - 1] = '\0';
    config->mqtt.password[sizeof(config->mqtt.password) - 1] = '\0';
    config->mqtt.root[sizeof(config->mqtt.root) - 1] = '\0';
    if (config->mqtt.address[0] == '\0')
    {
        copyBounded(config->mqtt.address, sizeof(config->mqtt.address), meshtastic_defaults::kDefaultMqttAddress);
    }
    if (config->mqtt.username[0] == '\0')
    {
        copyBounded(config->mqtt.username, sizeof(config->mqtt.username), meshtastic_defaults::kDefaultMqttUsername);
    }
    if (config->mqtt.password[0] == '\0')
    {
        copyBounded(config->mqtt.password, sizeof(config->mqtt.password), meshtastic_defaults::kDefaultMqttPassword);
    }
    if (config->mqtt.root[0] == '\0')
    {
        copyBounded(config->mqtt.root, sizeof(config->mqtt.root), meshtastic_defaults::kDefaultMqttRoot);
    }
    if (!config->mqtt.has_map_report_settings)
    {
        config->mqtt.has_map_report_settings = true;
        config->mqtt.map_report_settings.publish_interval_secs = meshtastic_defaults::kDefaultMapPublishIntervalSecs;
        config->mqtt.map_report_settings.position_precision = 0;
        config->mqtt.map_report_settings.should_report_location = false;
    }
}

inline void initDefaultModuleConfig(meshtastic_LocalModuleConfig* config, uint32_t self_node)
{
    if (!config)
    {
        return;
    }

    meshtastic_LocalModuleConfig zero = meshtastic_LocalModuleConfig_init_zero;
    *config = zero;
    config->version = meshtastic_defaults::kModuleConfigVersion;
    config->has_mqtt = true;
    config->has_serial = true;
    config->has_external_notification = true;
    config->has_store_forward = true;
    config->has_range_test = true;
    config->has_telemetry = true;
    config->has_canned_message = true;
    config->has_audio = true;
    config->has_remote_hardware = true;
    config->has_neighbor_info = true;
    config->has_ambient_lighting = true;
    config->has_detection_sensor = true;
    config->has_paxcounter = true;

    copyBounded(config->mqtt.address, sizeof(config->mqtt.address), meshtastic_defaults::kDefaultMqttAddress);
    copyBounded(config->mqtt.username, sizeof(config->mqtt.username), meshtastic_defaults::kDefaultMqttUsername);
    copyBounded(config->mqtt.password, sizeof(config->mqtt.password), meshtastic_defaults::kDefaultMqttPassword);
    copyBounded(config->mqtt.root, sizeof(config->mqtt.root), meshtastic_defaults::kDefaultMqttRoot);
    config->mqtt.enabled = false;
    config->mqtt.proxy_to_client_enabled = false;
    config->mqtt.encryption_enabled = meshtastic_defaults::kDefaultMqttEncryptionEnabled;
    config->mqtt.tls_enabled = meshtastic_defaults::kDefaultMqttTlsEnabled;
    config->mqtt.has_map_report_settings = true;
    config->mqtt.map_report_settings.publish_interval_secs = meshtastic_defaults::kDefaultMapPublishIntervalSecs;
    config->mqtt.map_report_settings.position_precision = 0;
    config->mqtt.map_report_settings.should_report_location = false;

    config->telemetry.device_update_interval = 3600;
    config->telemetry.device_telemetry_enabled = true;
    config->telemetry.environment_update_interval = 0;
    config->telemetry.environment_measurement_enabled = false;
    config->telemetry.power_update_interval = 0;
    config->telemetry.health_update_interval = 0;
    config->telemetry.air_quality_interval = 0;

    config->neighbor_info.enabled = false;
    config->neighbor_info.update_interval = 0;
    config->neighbor_info.transmit_over_lora = false;

    config->detection_sensor.enabled = false;
    config->detection_sensor.detection_trigger_type =
        meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH;
    config->detection_sensor.minimum_broadcast_secs = meshtastic_defaults::kDefaultDetectionMinBroadcastSecs;

    config->ambient_lighting.current = meshtastic_defaults::kDefaultAmbientCurrent;
    config->ambient_lighting.red = (self_node >> 16) & 0xFFU;
    config->ambient_lighting.green = (self_node >> 8) & 0xFFU;
    config->ambient_lighting.blue = self_node & 0xFFU;
}

inline void normalizeModuleConfig(meshtastic_LocalModuleConfig* config)
{
    if (!config)
    {
        return;
    }
    if (config->version == 0)
    {
        config->version = meshtastic_defaults::kModuleConfigVersion;
    }
    repairLegacyMqttConfig(config);
}

inline void initializeConfigState(const PersistedState& persisted,
                                  bool default_enabled,
                                  uint32_t self_node,
                                  meshtastic_Config_BluetoothConfig* out_bluetooth,
                                  meshtastic_LocalModuleConfig* out_module)
{
    if (out_bluetooth)
    {
        *out_bluetooth = meshtastic_Config_BluetoothConfig_init_zero;
        out_bluetooth->enabled = default_enabled;
        out_bluetooth->mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
        out_bluetooth->fixed_pin = 0;
        if (persisted.has_bluetooth)
        {
            *out_bluetooth = persisted.bluetooth;
        }
        normalizeBluetoothConfig(out_bluetooth);
    }

    if (out_module)
    {
        initDefaultModuleConfig(out_module, self_node);
        if (persisted.has_module)
        {
            *out_module = persisted.module;
            normalizeModuleConfig(out_module);
        }
    }
}

} // namespace ble::meshtastic_config_bridge
