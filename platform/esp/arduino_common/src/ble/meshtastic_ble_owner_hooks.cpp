#include "ble/meshtastic_ble.h"

#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/mt_adapter.h"
#include "platform/shared/ble/meshtastic_phone_runtime_bridge.h"

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
} // namespace

bool MeshtasticBleService::isBleConnected() const
{
    return connected_;
}

bool MeshtasticBleService::isPhoneBleConnected() const
{
    return connected_;
}

uint32_t MeshtasticBleService::pendingPhoneBlePasskey() const
{
    return pending_passkey_.load();
}

void MeshtasticBleService::requestPhoneHighThroughputConnection()
{
    requestHighThroughputConnection();
}

void MeshtasticBleService::requestPhoneLowerPowerConnection()
{
    requestLowerPowerConnection();
}

void MeshtasticBleService::onPhoneBluetoothConfigChanged()
{
    saveBleConfig();
}

void MeshtasticBleService::onPhoneModuleConfigChanged()
{
    saveModuleConfig();
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
        if (meshtastic_config_bridge::isValidBlePin(pin))
        {
            ble_config_.fixed_pin = pin;
        }
        prefs.end();
    }
    meshtastic_config_bridge::normalizeBluetoothConfig(&ble_config_);
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
        meshtastic_config_bridge::initDefaultModuleConfig(&module_config_, phone_facade_.getSelfNodeId());

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
        if (len != sizeof(module_config_) ||
            module_config_.version != ble::meshtastic_defaults::kModuleConfigVersion)
        {
            init_defaults();
        }
        else
        {
            meshtastic_config_bridge::normalizeModuleConfig(&module_config_);
        }
    }
}

void MeshtasticBleService::saveModuleConfig()
{
    meshtastic_config_bridge::normalizeModuleConfig(&module_config_);
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
    level = phone_facade_.getBatteryInfo().level;
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
    phone_facade_.syncMeshtasticMqttProxySettings(module_config_);
}

} // namespace ble
