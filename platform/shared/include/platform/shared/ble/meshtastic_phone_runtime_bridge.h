#pragma once

#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "gps/ports/i_location_source.h"
#include "meshtastic/localonly.pb.h"
#include "phone/common/phone_app_facade.h"
#include "phone/meshtastic/meshtastic_defaults.h"
#include "phone/meshtastic/meshtastic_phone_core.h"
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

inline std::string meshtasticChannelDisplayName(const ::phone::MeshtasticPhoneConfigSnapshot& cfg, uint8_t idx)
{
    return std::string(::chat::meshtastic::channelName(
        cfg.mesh,
        idx == 1 ? ::chat::ChannelId::SECONDARY : ::chat::ChannelId::PRIMARY));
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

inline bool setMeshtasticGpsFixUnavailable(::phone::meshtastic::MeshtasticGpsFix* out)
{
    if (!out)
    {
        return false;
    }

    *out = {};
    return false;
}

inline bool fillMeshtasticGpsFixFromLocationSource(const ::gps::ILocationSource& source,
                                                   ::phone::meshtastic::MeshtasticGpsFix* out)
{
    if (!out)
    {
        return false;
    }

    ::gps::LocationFix fix{};
    if (!source.latestFix(fix))
    {
        *out = {};
        return false;
    }

    out->valid = fix.valid;
    out->lat = fix.latitude;
    out->lng = fix.longitude;
    out->has_alt = fix.has_altitude;
    out->alt_m = static_cast<double>(fix.altitude_m);
    out->has_speed = fix.has_speed;
    out->speed_mps = static_cast<double>(fix.speed_mps);
    out->has_course = fix.has_course;
    out->course_deg = static_cast<double>(fix.course_deg);
    out->satellites = fix.satellites;
    return out->valid;
}

template <typename Settings>
inline void applyMeshtasticMqttProxySettings(Settings& settings,
                                             const meshtastic_LocalModuleConfig& module_config,
                                             const ::phone::MeshtasticPhoneConfigSnapshot& cfg)
{
    settings.enabled = module_config.has_mqtt && module_config.mqtt.enabled;
    settings.proxy_to_client_enabled = module_config.has_mqtt && module_config.mqtt.proxy_to_client_enabled;
    settings.encryption_enabled = module_config.has_mqtt ? module_config.mqtt.encryption_enabled : true;
    settings.primary_uplink_enabled = cfg.primary_uplink_enabled;
    settings.primary_downlink_enabled = cfg.primary_downlink_enabled;
    settings.secondary_uplink_enabled = cfg.secondary_uplink_enabled;
    settings.secondary_downlink_enabled = cfg.secondary_downlink_enabled;
    settings.root = module_config.mqtt.root[0] ? module_config.mqtt.root : ::phone::meshtastic::defaults::kDefaultMqttRoot;
    settings.primary_channel_id = meshtasticChannelDisplayName(cfg, 0);
    settings.secondary_channel_id = meshtasticChannelDisplayName(cfg, 1);
}

} // namespace platform::shared::ble_bridge
