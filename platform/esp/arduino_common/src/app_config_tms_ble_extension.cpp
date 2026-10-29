#include "platform/esp/arduino_common/app_config_tms_ble_extension.h"

#include "app/tms_compatibility_records.h"
#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "platform/esp/arduino_common/device_identity.h"

#include "platform/esp/common/config_preferences.h"
#include "platform/ui/settings_store.h"
#include <pb_decode.h>
#include <pb_encode.h>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace app::sd_tms::ble_extension
{
namespace
{

constexpr const char* kMtBleNamespace = "mt_ble";
constexpr const char* kMtModuleNamespace = "mt_mod";
constexpr const char* kMcBleNamespace = "mc_ble";
constexpr const char* kModuleBlobKey = "cfg";
using State = tms::CompatibilityRecords;

State* s_state_storage = nullptr;

#define s_state (*s_state_storage)

bool ensure_state()
{
    if (s_state_storage)
    {
        return true;
    }
#if defined(ESP_PLATFORM)
    void* const raw = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* const raw = std::malloc(sizeof(State));
#endif
    s_state_storage = raw ? new (raw) State{} : nullptr;
    return s_state_storage != nullptr;
}

void release_state()
{
    if (!s_state_storage)
    {
        return;
    }
    s_state_storage->~State();
#if defined(ESP_PLATFORM)
    heap_caps_free(s_state_storage);
#else
    std::free(s_state_storage);
#endif
    s_state_storage = nullptr;
}

void initialize_default_module(meshtastic_LocalModuleConfig* module)
{
    if (!module)
    {
        return;
    }
    // Do not call the convenience initializer here: it creates a full
    // LocalModuleConfig automatic temporary. This TMS extension runs before
    // services start and keeps its only module object in PSRAM instead.
    std::memset(module, 0, sizeof(*module));
    const uint32_t self_node =
        ::platform::esp::arduino_common::device_identity::getSelfNodeId();
    module->version = ble::meshtastic_defaults::kModuleConfigVersion;
    module->has_mqtt = true;
    module->has_serial = true;
    module->has_external_notification = true;
    module->has_store_forward = true;
    module->has_range_test = true;
    module->has_telemetry = true;
    module->has_canned_message = true;
    module->has_audio = true;
    module->has_remote_hardware = true;
    module->has_neighbor_info = true;
    module->has_ambient_lighting = true;
    module->has_detection_sensor = true;
    module->has_paxcounter = true;
    std::snprintf(module->mqtt.address,
                  sizeof(module->mqtt.address),
                  "%s",
                  ble::meshtastic_defaults::kDefaultMqttAddress);
    std::snprintf(module->mqtt.username,
                  sizeof(module->mqtt.username),
                  "%s",
                  ble::meshtastic_defaults::kDefaultMqttUsername);
    std::snprintf(module->mqtt.password,
                  sizeof(module->mqtt.password),
                  "%s",
                  ble::meshtastic_defaults::kDefaultMqttPassword);
    std::snprintf(module->mqtt.root,
                  sizeof(module->mqtt.root),
                  "%s",
                  ble::meshtastic_defaults::kDefaultMqttRoot);
    module->mqtt.enabled = false;
    module->mqtt.proxy_to_client_enabled = false;
    module->mqtt.encryption_enabled = ble::meshtastic_defaults::kDefaultMqttEncryptionEnabled;
    module->mqtt.tls_enabled = ble::meshtastic_defaults::kDefaultMqttTlsEnabled;
    module->mqtt.has_map_report_settings = true;
    module->mqtt.map_report_settings.publish_interval_secs =
        ble::meshtastic_defaults::kDefaultMapPublishIntervalSecs;
    module->mqtt.map_report_settings.position_precision = 0;
    module->mqtt.map_report_settings.should_report_location = false;
    module->telemetry.device_update_interval = 3600;
    module->telemetry.device_telemetry_enabled = true;
    module->telemetry.environment_update_interval = 0;
    module->telemetry.environment_measurement_enabled = false;
    module->telemetry.power_update_interval = 0;
    module->telemetry.health_update_interval = 0;
    module->telemetry.air_quality_interval = 0;
    module->neighbor_info.enabled = false;
    module->neighbor_info.update_interval = 0;
    module->neighbor_info.transmit_over_lora = false;
    module->detection_sensor.enabled = false;
    module->detection_sensor.detection_trigger_type =
        meshtastic_ModuleConfig_DetectionSensorConfig_TriggerType_LOGIC_HIGH;
    module->detection_sensor.minimum_broadcast_secs =
        ble::meshtastic_defaults::kDefaultDetectionMinBroadcastSecs;
    module->ambient_lighting.current = ble::meshtastic_defaults::kDefaultAmbientCurrent;
    module->ambient_lighting.red = (self_node >> 16U) & 0xFFU;
    module->ambient_lighting.green = (self_node >> 8U) & 0xFFU;
    module->ambient_lighting.blue = self_node & 0xFFU;
#if defined(ROTARY_A) && defined(ROTARY_B) && defined(ROTARY_C)
    module->canned_message.updown1_enabled = true;
    module->canned_message.inputbroker_pin_a = ROTARY_A;
    module->canned_message.inputbroker_pin_b = ROTARY_B;
    module->canned_message.inputbroker_pin_press = ROTARY_C;
    module->canned_message.inputbroker_event_cw =
        meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(28);
    module->canned_message.inputbroker_event_ccw =
        meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar(29);
    module->canned_message.inputbroker_event_press =
        meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT;
#endif
}

void reset_state()
{
    s_state_storage->~State();
    new (s_state_storage) State();
    s_state.mt_ble.enabled = true;
    s_state.mt_ble.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    initialize_default_module(&s_state.module);
}

bool valid_mc_pin(uint32_t pin)
{
    return s_state.valid_mc_pin(pin);
}

bool valid_state()
{
    return s_state.valid_state();
}

bool load_preferences()
{
    reset_state();
    namespace store = ::platform::ui::settings_store;
    const auto boolean = [](const char* ns, const char* key, bool fallback, bool& out)
    {
        const auto result = store::read_bool_into(ns, key, &out);
        if (result == store::ScalarReadResult::Missing)
        {
            out = fallback;
            return true;
        }
        return result == store::ScalarReadResult::Ok;
    };
    const auto byte = [](const char* ns, const char* key, uint8_t fallback, uint8_t& out)
    {
        const auto result = store::read_u8_into(ns, key, &out);
        if (result == store::ScalarReadResult::Missing)
        {
            out = fallback;
            return true;
        }
        return result == store::ScalarReadResult::Ok;
    };
    const auto integer = [](const char* ns, const char* key, uint32_t fallback, uint32_t& out)
    {
        const auto result = store::read_uint_into(ns, key, &out);
        if (result == store::ScalarReadResult::Missing)
        {
            out = fallback;
            return true;
        }
        return result == store::ScalarReadResult::Ok;
    };
    uint8_t mode = 0;
    if (!boolean(kMtBleNamespace, "enabled", true, s_state.mt_ble.enabled) ||
        !byte(kMtBleNamespace, "mode", static_cast<uint8_t>(meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN), mode) ||
        !integer(kMtBleNamespace, "pin", 0U, s_state.mt_ble.fixed_pin) ||
        !ble::meshtastic_config_bridge::isValidBluetoothMode(mode) ||
        !ble::meshtastic_config_bridge::isValidBlePin(s_state.mt_ble.fixed_pin) ||
        (mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN && s_state.mt_ble.fixed_pin != 0U)) return false;
    s_state.mt_ble.mode = static_cast<meshtastic_Config_BluetoothConfig_PairingMode>(mode);
    if (!integer(kMcBleNamespace, "pin", 0U, s_state.mc_pin) || !valid_mc_pin(s_state.mc_pin) ||
        !boolean(kMcBleNamespace, "manual_add", false, s_state.mc_manual_add) ||
        !byte(kMcBleNamespace, "telem_base", 0U, s_state.mc_telemetry_base) ||
        !byte(kMcBleNamespace, "telem_loc", 0U, s_state.mc_telemetry_loc) ||
        !byte(kMcBleNamespace, "telem_env", 0U, s_state.mc_telemetry_env) ||
        !byte(kMcBleNamespace, "advert_loc", 0U, s_state.mc_advert_location) ||
        s_state.mc_telemetry_base > 3U || s_state.mc_telemetry_loc > 3U || s_state.mc_telemetry_env > 3U) return false;

    // Defaults were staged by reset_state. Preserve them only for an absent
    // module; a failed/truncated/future-version read must not replace user data.
    std::size_t size = 0;
    const auto result = store::read_blob_into(kMtModuleNamespace, kModuleBlobKey,
                                              &s_state.module, sizeof(s_state.module), &size);
    if (result == store::BlobReadResult::Missing) return true;
    if (result != store::BlobReadResult::Ok || size != sizeof(s_state.module) ||
        (s_state.module.version != 0U && s_state.module.version != ble::meshtastic_defaults::kModuleConfigVersion)) return false;
    // Version zero is the explicitly supported legacy form. The existing
    // normalizer upgrades it without discarding unrelated module settings.
    ble::meshtastic_config_bridge::normalizeModuleConfig(&s_state.module);
    return s_state.module.version == ble::meshtastic_defaults::kModuleConfigVersion;
}

bool write_preferences()
{
    ::platform::esp::common::ConfigPreferences prefs;
    if (!prefs.begin(kMtBleNamespace, false))
    {
        return false;
    }
    bool ok = prefs.putBool("enabled", s_state.mt_ble.enabled) &&
              prefs.putUChar("mode", static_cast<uint8_t>(s_state.mt_ble.mode)) &&
              prefs.putUInt("pin", s_state.mt_ble.fixed_pin);
    prefs.end();
    if (!ok)
    {
        return false;
    }
    if (!prefs.begin(kMcBleNamespace, false))
    {
        return false;
    }
    ok = prefs.putUInt("pin", s_state.mc_pin) &&
         prefs.putBool("manual_add", s_state.mc_manual_add) &&
         prefs.putUChar("telem_base", s_state.mc_telemetry_base) &&
         prefs.putUChar("telem_loc", s_state.mc_telemetry_loc) &&
         prefs.putUChar("telem_env", s_state.mc_telemetry_env) &&
         prefs.putUChar("advert_loc", s_state.mc_advert_location);
    prefs.end();
    if (!ok)
    {
        return false;
    }
    if (!prefs.begin(kMtModuleNamespace, false))
    {
        return false;
    }
    ok = prefs.putBytes(kModuleBlobKey, &s_state.module, sizeof(s_state.module)) ==
         sizeof(s_state.module);
    prefs.end();
    return ok;
}

} // namespace

bool beginRead()
{
    if (!ensure_state())
    {
        return false;
    }
    reset_state();
    return true;
}

void endRead()
{
    release_state();
}

tms::RecordConsumeResult consumeRecord(const tms::RecordReader& reader)
{
    if (!s_state_storage) return tms::RecordConsumeResult::Invalid;
    return s_state.consumeRecord(reader);
}

bool finishDocument(bool applying, uint16_t schema_version)
{
    if (!s_state_storage)
    {
        return false;
    }
    if (s_state.seen == 0U)
    {
        return schema_version != tms::kSchemaVersion;
    }
    if (!valid_state())
    {
        return false;
    }
    return !applying || write_preferences();
}

bool writeRecords(tms::RecordWriter& writer)
{
    if (!beginRead() || !load_preferences())
    {
        endRead();
        return false;
    }
    const bool ok = s_state.writeRecords(writer);
    endRead();
    return ok;
}

#undef s_state

} // namespace app::sd_tms::ble_extension
