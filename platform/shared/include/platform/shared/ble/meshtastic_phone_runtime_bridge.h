#pragma once

#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_core.h"
#include "chat/ble/phone_runtime_context.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "meshtastic/localonly.pb.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/time_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace platform::shared::ble_bridge
{

constexpr const char* kUiSettingsNs = "settings";
constexpr const char* kTimezoneTzdefKey = "timezone_tzdef";

inline std::string meshtasticChannelDisplayName(const ::ble::MeshtasticPhoneConfigSnapshot& cfg, uint8_t idx)
{
    if (idx == 1)
    {
        return "Secondary";
    }

    auto preset = static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.mesh.modem_preset);
    return cfg.mesh.use_preset ? std::string(::chat::meshtastic::presetDisplayName(preset)) : std::string("Custom");
}

inline bool loadTimezoneTzdefFromSettings(char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }

    out[0] = '\0';
    std::vector<uint8_t> blob;
    if (!platform::ui::settings_store::get_blob(kUiSettingsNs, kTimezoneTzdefKey, blob) || blob.empty())
    {
        return false;
    }

    const std::size_t copy_len = std::min(out_len - 1U, blob.size());
    std::memcpy(out, blob.data(), copy_len);
    out[copy_len] = '\0';
    return out[0] != '\0';
}

inline void saveTimezoneTzdefToSettings(const char* tzdef)
{
    if (!tzdef || tzdef[0] == '\0')
    {
        const char* keys[] = {kTimezoneTzdefKey};
        platform::ui::settings_store::remove_keys(kUiSettingsNs, keys, 1);
        return;
    }

    (void)platform::ui::settings_store::put_blob(kUiSettingsNs, kTimezoneTzdefKey, tzdef, std::strlen(tzdef) + 1U);
}

inline int getTimezoneOffsetMinutes()
{
    return platform::ui::time::timezone_offset_min();
}

inline void setTimezoneOffsetMinutes(int offset_min)
{
    platform::ui::time::set_timezone_offset_min(offset_min);
}

inline bool setMeshtasticGpsFixUnavailable(::ble::MeshtasticGpsFix* out)
{
    if (!out)
    {
        return false;
    }

    *out = {};
    return false;
}

inline bool fillMeshtasticGpsFixFromUiRuntime(::ble::MeshtasticGpsFix* out)
{
    if (!out)
    {
        return false;
    }

    const platform::ui::gps::GpsState gps_state = platform::ui::gps::get_data();
    out->valid = gps_state.valid;
    out->lat = gps_state.lat;
    out->lng = gps_state.lng;
    out->has_alt = gps_state.has_alt;
    out->alt_m = gps_state.alt_m;
    out->has_speed = gps_state.has_speed;
    out->speed_mps = gps_state.speed_mps;
    out->has_course = gps_state.has_course;
    out->course_deg = gps_state.course_deg;
    out->satellites = gps_state.satellites;
    return out->valid;
}

template <typename Settings>
inline void applyMeshtasticMqttProxySettings(Settings& settings,
                                             const meshtastic_LocalModuleConfig& module_config,
                                             const ::ble::MeshtasticPhoneConfigSnapshot& cfg)
{
    settings.enabled = module_config.has_mqtt && module_config.mqtt.enabled;
    settings.proxy_to_client_enabled = module_config.has_mqtt && module_config.mqtt.proxy_to_client_enabled;
    settings.encryption_enabled = module_config.has_mqtt ? module_config.mqtt.encryption_enabled : true;
    settings.primary_uplink_enabled = cfg.primary_uplink_enabled;
    settings.primary_downlink_enabled = cfg.primary_downlink_enabled;
    settings.secondary_uplink_enabled = cfg.secondary_uplink_enabled;
    settings.secondary_downlink_enabled = cfg.secondary_downlink_enabled;
    settings.root = module_config.mqtt.root[0] ? module_config.mqtt.root : ::ble::meshtastic_defaults::kDefaultMqttRoot;
    settings.primary_channel_id = meshtasticChannelDisplayName(cfg, 0);
    settings.secondary_channel_id = meshtasticChannelDisplayName(cfg, 1);
}

} // namespace platform::shared::ble_bridge
