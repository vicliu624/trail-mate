#include "ui/screens/settings/settings_spec.h"

#include "chat/infra/mesh_protocol_utils.h"
#include <cstring>

namespace settings::ui::spec
{

namespace
{

struct SettingSpec
{
    SettingId id;
    const char* key;
    DynamicOptionKind dynamic_options;
};

constexpr SettingSpec kSpecs[] = {
    {SettingId::ChatUser, "chat_user", DynamicOptionKind::None},
    {SettingId::ChatShort, "chat_short", DynamicOptionKind::None},
    {SettingId::MeshProtocol, "mesh_protocol", DynamicOptionKind::None},
    {SettingId::ChatMessageAlerts, "chat_message_alerts", DynamicOptionKind::None},
    {SettingId::ChatContactAlerts, "chat_contact_alerts", DynamicOptionKind::None},
    {SettingId::ChatAutoReplyEnabled, "chat_auto_reply_enabled", DynamicOptionKind::None},
    {SettingId::ChatAutoReplyText, "chat_auto_reply_text", DynamicOptionKind::None},
    {SettingId::ChatRegion, "chat_region", DynamicOptionKind::ChatRegion},
    {SettingId::ChatChannel, "chat_channel", DynamicOptionKind::None},
    {SettingId::ChatPsk, "chat_psk", DynamicOptionKind::None},
    {SettingId::MtPrimaryEnabled, "mt_primary_enabled", DynamicOptionKind::None},
    {SettingId::MtPrimaryName, "mt_primary_name", DynamicOptionKind::None},
    {SettingId::MtPrimaryKey, "mt_primary_key", DynamicOptionKind::None},
    {SettingId::MtPrimaryKeyGenerate, "mt_primary_key_generate", DynamicOptionKind::None},
    {SettingId::MtPrimaryUplink, "mt_primary_uplink", DynamicOptionKind::None},
    {SettingId::MtPrimaryDownlink, "mt_primary_downlink", DynamicOptionKind::None},
    {SettingId::MtSecondaryEnabled, "mt_secondary_enabled", DynamicOptionKind::None},
    {SettingId::MtSecondaryName, "mt_secondary_name", DynamicOptionKind::None},
    {SettingId::MtSecondaryKey, "mt_secondary_key", DynamicOptionKind::None},
    {SettingId::MtSecondaryKeyGenerate, "mt_secondary_key_generate", DynamicOptionKind::None},
    {SettingId::MtSecondaryUplink, "mt_secondary_uplink", DynamicOptionKind::None},
    {SettingId::MtSecondaryDownlink, "mt_secondary_downlink", DynamicOptionKind::None},
    {SettingId::PrivacyEncrypt, "privacy_encrypt", DynamicOptionKind::None},
    {SettingId::MtMqttEnabled, "mt_mqtt_enabled", DynamicOptionKind::None},
    {SettingId::MtMqttHost, "mt_mqtt_host", DynamicOptionKind::None},
    {SettingId::MtMqttPort, "mt_mqtt_port", DynamicOptionKind::None},
    {SettingId::MtMqttRoot, "mt_mqtt_root", DynamicOptionKind::None},
    {SettingId::MtMqttUser, "mt_mqtt_user", DynamicOptionKind::None},
    {SettingId::MtMqttPass, "mt_mqtt_pass", DynamicOptionKind::None},
    {SettingId::MtMqttUplink, "mt_mqtt_uplink", DynamicOptionKind::None},
    {SettingId::MtMqttDownlink, "mt_mqtt_downlink", DynamicOptionKind::None},
    {SettingId::McMqttEnabled, "mc_mqtt_enabled", DynamicOptionKind::None},
    {SettingId::McMqttHost, "mc_mqtt_host", DynamicOptionKind::None},
    {SettingId::McMqttPort, "mc_mqtt_port", DynamicOptionKind::None},
    {SettingId::McMqttRoot, "mc_mqtt_root", DynamicOptionKind::None},
    {SettingId::McMqttUser, "mc_mqtt_user", DynamicOptionKind::None},
    {SettingId::McMqttPass, "mc_mqtt_pass", DynamicOptionKind::None},
    {SettingId::McMqttUplink, "mc_mqtt_uplink", DynamicOptionKind::None},
    {SettingId::McMqttDownlink, "mc_mqtt_downlink", DynamicOptionKind::None},
    {SettingId::McChannelSlot, "mc_channel_slot", DynamicOptionKind::None},
    {SettingId::McChannelEnabled, "mc_channel_enabled", DynamicOptionKind::None},
    {SettingId::McChannelName, "mc_channel_name", DynamicOptionKind::None},
    {SettingId::McChannelKey, "mc_channel_key", DynamicOptionKind::None},
    {SettingId::McChannelKeyGenerate, "mc_channel_key_generate", DynamicOptionKind::None},
    {SettingId::McChannelClear, "mc_channel_clear", DynamicOptionKind::None},
    {SettingId::RtBearer, "rt_bearer", DynamicOptionKind::None},
    {SettingId::RtLoraEnabled, "rt_lora_enabled", DynamicOptionKind::None},
    {SettingId::RtDisplayName, "rt_display_name", DynamicOptionKind::None},
    {SettingId::RtIdentityHash, "rt_identity_hash", DynamicOptionKind::None},
    {SettingId::RtLxmfAddress, "rt_lxmf_address", DynamicOptionKind::None},
    {SettingId::RtWifiGateway, "rt_wifi_gateway", DynamicOptionKind::None},
    {SettingId::RtTcpSlot, "rt_tcp_slot", DynamicOptionKind::None},
    {SettingId::RtTcpDefaults, "rt_tcp_defaults", DynamicOptionKind::None},
    {SettingId::RtWifiHost, "rt_wifi_host", DynamicOptionKind::None},
    {SettingId::RtWifiPort, "rt_wifi_port", DynamicOptionKind::None},
    {SettingId::RtWifiAuto, "rt_wifi_auto", DynamicOptionKind::None},
    {SettingId::RtAnonymousPeer, "rt_anonymous_peer", DynamicOptionKind::None},
    {SettingId::RtLocationRequests, "rt_location_requests", DynamicOptionKind::None},
    {SettingId::NetUsePreset, "net_use_preset", DynamicOptionKind::None},
    {SettingId::NetPreset, "net_preset", DynamicOptionKind::None},
    {SettingId::NetBw, "net_bw", DynamicOptionKind::None},
    {SettingId::NetSf, "net_sf", DynamicOptionKind::None},
    {SettingId::NetCr, "net_cr", DynamicOptionKind::None},
    {SettingId::NetTxPower, "net_tx_power", DynamicOptionKind::TxPower},
    {SettingId::NetHopLimit, "net_hop_limit", DynamicOptionKind::None},
    {SettingId::NetTxEnabled, "net_tx_enabled", DynamicOptionKind::None},
    {SettingId::NetOverrideDuty, "net_override_duty", DynamicOptionKind::None},
    {SettingId::NetChannelNum, "net_channel_num", DynamicOptionKind::None},
    {SettingId::NetFreqOffset, "net_freq_offset", DynamicOptionKind::None},
    {SettingId::NetOverrideFreq, "net_override_freq", DynamicOptionKind::None},
    {SettingId::NetRelay, "net_relay", DynamicOptionKind::None},
    {SettingId::NetDutyCycle, "net_duty_cycle", DynamicOptionKind::None},
    {SettingId::NetUtil, "net_util", DynamicOptionKind::None},
    {SettingId::McRegionPreset, "mc_region_preset", DynamicOptionKind::MeshCoreRegionPreset},
    {SettingId::McFreq, "mc_freq", DynamicOptionKind::None},
    {SettingId::McBw, "mc_bw", DynamicOptionKind::None},
    {SettingId::McSf, "mc_sf", DynamicOptionKind::None},
    {SettingId::McCr, "mc_cr", DynamicOptionKind::None},
    {SettingId::McTxPower, "mc_tx_power", DynamicOptionKind::TxPower},
    {SettingId::McRepeat, "mc_repeat", DynamicOptionKind::None},
    {SettingId::McRxDelay, "mc_rx_delay", DynamicOptionKind::None},
    {SettingId::McAirtime, "mc_airtime", DynamicOptionKind::None},
    {SettingId::McFloodMax, "mc_flood_max", DynamicOptionKind::None},
    {SettingId::McMultiAcks, "mc_multi_acks", DynamicOptionKind::None},
    {SettingId::McSendProfile, "mc_send_prof", DynamicOptionKind::None},
    {SettingId::McForwardProfile, "mc_fwd_prof", DynamicOptionKind::None},
    {SettingId::GpsEnabled, "gps_enabled", DynamicOptionKind::None},
    {SettingId::GpsInitBaud, "gps_init_baud", DynamicOptionKind::None},
    {SettingId::GpsInitProbeMs, "gps_init_probe_ms", DynamicOptionKind::None},
    {SettingId::GpsInitProfile, "gps_init_profile", DynamicOptionKind::None},
    {SettingId::GpsInitRxm, "gps_init_rxm", DynamicOptionKind::None},
    {SettingId::GpsInitGnss, "gps_init_gnss", DynamicOptionKind::None},
    {SettingId::GpsInitNmea, "gps_init_nmea", DynamicOptionKind::None},
    {SettingId::GpsMode, "gps_mode", DynamicOptionKind::None},
    {SettingId::GpsSatMask, "gps_sat_mask", DynamicOptionKind::None},
    {SettingId::GpsStrategy, "gps_strategy", DynamicOptionKind::None},
    {SettingId::GpsInterval, "gps_interval", DynamicOptionKind::None},
    {SettingId::GpsAltRef, "gps_alt_ref", DynamicOptionKind::None},
    {SettingId::GpsCoordFmt, "gps_coord_fmt", DynamicOptionKind::None},
    {SettingId::ExternalNmea, "external_nmea", DynamicOptionKind::None},
    {SettingId::ExternalNmeaSent, "external_nmea_sent", DynamicOptionKind::None},
    {SettingId::GpsDiagnostics, "gps_diagnostics", DynamicOptionKind::None},
    {SettingId::MapCoord, "map_coord", DynamicOptionKind::None},
    {SettingId::MapSource, "map_source", DynamicOptionKind::None},
    {SettingId::MapContour, "map_contour", DynamicOptionKind::None},
    {SettingId::MapTrack, "map_track", DynamicOptionKind::None},
    {SettingId::MapTrackInterval, "map_track_interval", DynamicOptionKind::None},
    {SettingId::MapTrackFormat, "map_track_format", DynamicOptionKind::None},
    {SettingId::DisplayLocale, "display_locale", DynamicOptionKind::Locale},
    {SettingId::EnabledImes, "enabled_imes", DynamicOptionKind::None},
    {SettingId::ScreenTimeout, "screen_timeout", DynamicOptionKind::None},
    {SettingId::ScreenBrightness, "screen_brightness", DynamicOptionKind::None},
    {SettingId::SpeakerVolume, "speaker_volume", DynamicOptionKind::None},
    {SettingId::VibrationEnabled, "vibration_enabled", DynamicOptionKind::None},
    {SettingId::C6CompanionStatus, "c6_companion_status", DynamicOptionKind::None},
    {SettingId::C6EnterDownload, "c6_enter_download", DynamicOptionKind::None},
    {SettingId::TimezoneProfile, "timezone_profile", DynamicOptionKind::TimeZone},
    {SettingId::ManualTimeSet, "manual_time_set", DynamicOptionKind::None},
    {SettingId::GaugeDesignMah, "gauge_design_mah", DynamicOptionKind::None},
    {SettingId::GaugeFullMah, "gauge_full_mah", DynamicOptionKind::None},
    {SettingId::SpiDiagnostics, "spi_diagnostics", DynamicOptionKind::None},
    {SettingId::WifiEnabled, "wifi_enabled", DynamicOptionKind::None},
    {SettingId::WifiStatus, "wifi_status", DynamicOptionKind::None},
    {SettingId::WifiScan, "wifi_scan", DynamicOptionKind::None},
    {SettingId::WifiNetwork, "wifi_network", DynamicOptionKind::WifiNetwork},
    {SettingId::WifiSsid, "wifi_ssid", DynamicOptionKind::None},
    {SettingId::WifiPassword, "wifi_password", DynamicOptionKind::None},
    {SettingId::WifiConnect, "wifi_connect", DynamicOptionKind::None},
    {SettingId::WifiDisconnect, "wifi_disconnect", DynamicOptionKind::None},
    {SettingId::FwCurrent, "fw_current", DynamicOptionKind::None},
    {SettingId::FwLatest, "fw_latest", DynamicOptionKind::None},
    {SettingId::FwStatus, "fw_status", DynamicOptionKind::None},
    {SettingId::FwCheck, "fw_check", DynamicOptionKind::None},
    {SettingId::FwInstall, "fw_install", DynamicOptionKind::None},
    {SettingId::AdvDebug, "adv_debug", DynamicOptionKind::None},
    {SettingId::ChatResetMesh, "chat_reset_mesh", DynamicOptionKind::None},
    {SettingId::ChatResetNodes, "chat_reset_nodes", DynamicOptionKind::None},
    {SettingId::ChatClearMessages, "chat_clear_messages", DynamicOptionKind::None},
    {SettingId::SystemFactoryReset, "system_factory_reset", DynamicOptionKind::None},
};

const SettingSpec* find_spec(SettingId id)
{
    for (const SettingSpec& spec : kSpecs)
    {
        if (spec.id == id)
        {
            return &spec;
        }
    }
    return nullptr;
}

bool is_meshtastic_protocol(const VisibilityContext& context)
{
    return context.protocol == chat::MeshProtocol::Meshtastic;
}

bool is_meshcore_protocol(const VisibilityContext& context)
{
    return context.protocol == chat::MeshProtocol::MeshCore;
}

bool is_reticulum_protocol(const VisibilityContext& context)
{
    return chat::infra::isReticulumMeshProtocol(context.protocol);
}

bool is_meshtastic_mqtt(SettingId id)
{
    switch (id)
    {
    case SettingId::MtMqttEnabled:
    case SettingId::MtMqttHost:
    case SettingId::MtMqttPort:
    case SettingId::MtMqttRoot:
    case SettingId::MtMqttUser:
    case SettingId::MtMqttPass:
    case SettingId::MtMqttUplink:
    case SettingId::MtMqttDownlink:
        return true;
    default:
        return false;
    }
}

bool is_meshcore_mqtt(SettingId id)
{
    switch (id)
    {
    case SettingId::McMqttEnabled:
    case SettingId::McMqttHost:
    case SettingId::McMqttPort:
    case SettingId::McMqttRoot:
    case SettingId::McMqttUser:
    case SettingId::McMqttPass:
    case SettingId::McMqttUplink:
    case SettingId::McMqttDownlink:
        return true;
    default:
        return false;
    }
}

bool is_meshtastic_channel(SettingId id)
{
    switch (id)
    {
    case SettingId::ChatRegion:
    case SettingId::ChatChannel:
    case SettingId::ChatPsk:
    case SettingId::MtPrimaryEnabled:
    case SettingId::MtPrimaryName:
    case SettingId::MtPrimaryKey:
    case SettingId::MtPrimaryKeyGenerate:
    case SettingId::MtPrimaryUplink:
    case SettingId::MtPrimaryDownlink:
    case SettingId::MtSecondaryEnabled:
    case SettingId::MtSecondaryName:
    case SettingId::MtSecondaryKey:
    case SettingId::MtSecondaryKeyGenerate:
    case SettingId::MtSecondaryUplink:
    case SettingId::MtSecondaryDownlink:
    case SettingId::PrivacyEncrypt:
        return true;
    default:
        return false;
    }
}

bool is_meshcore_channel(SettingId id)
{
    switch (id)
    {
    case SettingId::McChannelSlot:
    case SettingId::McChannelEnabled:
    case SettingId::McChannelName:
    case SettingId::McChannelKey:
    case SettingId::McChannelKeyGenerate:
    case SettingId::McChannelClear:
        return true;
    default:
        return false;
    }
}

bool is_reticulum_mesh(SettingId id)
{
    switch (id)
    {
    case SettingId::RtBearer:
    case SettingId::RtLoraEnabled:
    case SettingId::RtDisplayName:
    case SettingId::RtIdentityHash:
    case SettingId::RtLxmfAddress:
    case SettingId::RtWifiGateway:
    case SettingId::RtTcpSlot:
    case SettingId::RtWifiHost:
    case SettingId::RtWifiPort:
    case SettingId::RtWifiAuto:
    case SettingId::RtAnonymousPeer:
    case SettingId::RtLocationRequests:
        return true;
    default:
        return false;
    }
}

bool is_meshcore_radio(SettingId id)
{
    switch (id)
    {
    case SettingId::McRegionPreset:
    case SettingId::McFreq:
    case SettingId::McBw:
    case SettingId::McSf:
    case SettingId::McCr:
    case SettingId::McTxPower:
    case SettingId::McRepeat:
    case SettingId::McRxDelay:
    case SettingId::McAirtime:
    case SettingId::McFloodMax:
    case SettingId::McMultiAcks:
    case SettingId::McSendProfile:
    case SettingId::McForwardProfile:
        return true;
    default:
        return false;
    }
}

bool is_lora_radio(SettingId id)
{
    switch (id)
    {
    case SettingId::NetUsePreset:
    case SettingId::NetPreset:
    case SettingId::NetBw:
    case SettingId::NetSf:
    case SettingId::NetCr:
    case SettingId::NetTxPower:
    case SettingId::NetHopLimit:
    case SettingId::NetTxEnabled:
    case SettingId::NetOverrideDuty:
    case SettingId::NetChannelNum:
    case SettingId::NetFreqOffset:
    case SettingId::NetOverrideFreq:
    case SettingId::NetDutyCycle:
    case SettingId::NetUtil:
        return true;
    default:
        return false;
    }
}

bool is_mt_secondary_child(SettingId id)
{
    switch (id)
    {
    case SettingId::MtSecondaryName:
    case SettingId::MtSecondaryKey:
    case SettingId::MtSecondaryKeyGenerate:
    case SettingId::MtSecondaryUplink:
    case SettingId::MtSecondaryDownlink:
        return true;
    default:
        return false;
    }
}

bool is_reticulum_wifi_detail(SettingId id)
{
    switch (id)
    {
    case SettingId::RtTcpSlot:
    case SettingId::RtWifiHost:
    case SettingId::RtWifiPort:
    case SettingId::RtWifiAuto:
        return true;
    default:
        return false;
    }
}

bool is_wifi_setting(SettingId id)
{
    switch (id)
    {
    case SettingId::WifiEnabled:
    case SettingId::WifiScan:
    case SettingId::WifiConnect:
    case SettingId::WifiDisconnect:
    case SettingId::WifiSsid:
    case SettingId::WifiPassword:
        return true;
    default:
        return false;
    }
}

bool is_firmware_update_setting(SettingId id)
{
    switch (id)
    {
    case SettingId::FwCurrent:
    case SettingId::FwLatest:
    case SettingId::FwStatus:
    case SettingId::FwCheck:
    case SettingId::FwInstall:
        return true;
    default:
        return false;
    }
}

bool is_wireless_companion_setting(SettingId id)
{
    switch (id)
    {
    case SettingId::C6CompanionStatus:
    case SettingId::C6EnterDownload:
        return true;
    default:
        return false;
    }
}

bool is_gps_init_policy_setting(SettingId id)
{
    switch (id)
    {
    case SettingId::GpsInitProbeMs:
    case SettingId::GpsInitProfile:
    case SettingId::GpsInitRxm:
    case SettingId::GpsInitGnss:
    case SettingId::GpsInitNmea:
        return true;
    default:
        return false;
    }
}

bool is_gps_gnss_setting(SettingId id)
{
    switch (id)
    {
    case SettingId::GpsMode:
    case SettingId::GpsSatMask:
    case SettingId::GpsStrategy:
        return true;
    default:
        return false;
    }
}

bool is_external_nmea_setting(SettingId id)
{
    return id == SettingId::ExternalNmea || id == SettingId::ExternalNmeaSent;
}

bool is_gauge_setting(SettingId id)
{
    return id == SettingId::GaugeDesignMah || id == SettingId::GaugeFullMah;
}

} // namespace

SettingId id_for_key(const char* pref_key)
{
    if (!pref_key)
    {
        return SettingId::Unknown;
    }
    for (const SettingSpec& spec : kSpecs)
    {
        if (std::strcmp(spec.key, pref_key) == 0)
        {
            return spec.id;
        }
    }
    return SettingId::Unknown;
}

const char* key_for_id(SettingId id)
{
    const SettingSpec* spec = find_spec(id);
    return spec ? spec->key : nullptr;
}

void bind_item(SettingItem& item)
{
    if (item.id == SettingId::Unknown)
    {
        item.id = id_for_key(item.pref_key);
    }
}

void bind_items(SettingItem* items, std::size_t count)
{
    if (!items)
    {
        return;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        bind_item(items[index]);
    }
}

DynamicOptionKind dynamic_option_kind(SettingId id)
{
    const SettingSpec* spec = find_spec(id);
    return spec ? spec->dynamic_options : DynamicOptionKind::None;
}

bool option_labels_are_translated(SettingId id)
{
    return id != SettingId::DisplayLocale && id != SettingId::WifiNetwork;
}

bool option_labels_use_content_font(SettingId id)
{
    return id == SettingId::DisplayLocale || id == SettingId::WifiNetwork;
}

bool is_settings_store_owned_enum(SettingId id)
{
    switch (id)
    {
    case SettingId::ScreenBrightness:
    case SettingId::SpeakerVolume:
    case SettingId::ChatMessageAlerts:
    case SettingId::ChatContactAlerts:
        return true;
    default:
        return false;
    }
}

bool is_settings_store_owned_toggle(SettingId id)
{
    return id == SettingId::ChatAutoReplyEnabled ||
           id == SettingId::VibrationEnabled || id == SettingId::AdvDebug;
}

bool should_show(SettingId id, const VisibilityContext& context)
{
    // Geocaching uses Reticulum independently of the selected chat protocol.
    // Keep IP configuration editable even when the current bearer is LoRa-only.
    if (id == SettingId::RtTcpSlot || id == SettingId::RtTcpDefaults ||
        id == SettingId::RtWifiHost || id == SettingId::RtWifiPort)
    {
        return context.wifi_supported;
    }
    if (id == SettingId::Unknown)
    {
        return true;
    }
    if (id == SettingId::WifiNetwork)
    {
        return context.wifi_supported && context.has_wifi_networks;
    }
    if (is_wifi_setting(id) && !context.wifi_supported)
    {
        return false;
    }
    if (is_firmware_update_setting(id) && !context.firmware_update_supported)
    {
        return false;
    }
    if (is_wireless_companion_setting(id) && !context.wireless_companion_supported)
    {
        return false;
    }

    const bool meshtastic = is_meshtastic_protocol(context);
    const bool meshcore = is_meshcore_protocol(context);
    const bool reticulum = is_reticulum_protocol(context);

    if (is_meshtastic_mqtt(id) && !meshtastic)
    {
        return false;
    }
    if (is_meshcore_mqtt(id) && !meshcore)
    {
        return false;
    }
    if (is_meshtastic_channel(id) && !meshtastic)
    {
        return false;
    }
    if (is_meshcore_channel(id) && !meshcore)
    {
        return false;
    }
    if (is_reticulum_mesh(id) && !reticulum)
    {
        return false;
    }
    if (is_meshcore_radio(id) && !meshcore)
    {
        return false;
    }
    if (is_lora_radio(id))
    {
        if (meshcore)
        {
            return false;
        }
        if (reticulum && !context.reticulum_lora_visible)
        {
            return false;
        }
    }
    if (is_mt_secondary_child(id) && !context.mt_secondary_enabled)
    {
        return false;
    }
    if (is_reticulum_wifi_detail(id) && reticulum && !context.reticulum_wifi_visible)
    {
        return false;
    }
    if (id == SettingId::NetRelay)
    {
        return false;
    }

    if (id == SettingId::ScreenBrightness && !context.screen_brightness_supported)
    {
        return false;
    }
    if (id == SettingId::ScreenTimeout && !context.screen_timeout_supported)
    {
        return false;
    }
    if (id == SettingId::GpsInitBaud && !context.gps_baud_supported)
    {
        return false;
    }
    if (is_gps_init_policy_setting(id) && !context.gps_init_policy_supported)
    {
        return false;
    }
    if (is_gps_gnss_setting(id) && !context.gps_gnss_supported)
    {
        return false;
    }
    if (id == SettingId::GpsInterval && !context.gps_interval_supported)
    {
        return false;
    }
    if (id == SettingId::GpsAltRef && !context.gps_alt_ref_supported)
    {
        return false;
    }
    if (id == SettingId::GpsCoordFmt && !context.gps_coord_format_supported)
    {
        return false;
    }
    if (is_external_nmea_setting(id) && !context.external_nmea_supported)
    {
        return false;
    }
    if (is_gauge_setting(id) && !context.battery_gauge_supported)
    {
        return false;
    }

    if (meshcore)
    {
        switch (id)
        {
        case SettingId::NetOverrideDuty:
        case SettingId::NetChannelNum:
        case SettingId::NetFreqOffset:
        case SettingId::NetOverrideFreq:
            return false;
        default:
            break;
        }
    }
    else if (reticulum)
    {
        switch (id)
        {
        case SettingId::NetUsePreset:
        case SettingId::NetPreset:
        case SettingId::NetHopLimit:
        case SettingId::NetOverrideDuty:
        case SettingId::NetChannelNum:
        case SettingId::NetFreqOffset:
        case SettingId::NetDutyCycle:
        case SettingId::NetUtil:
        case SettingId::RtLoraEnabled:
        case SettingId::RtWifiGateway:
            return false;
        default:
            break;
        }
    }
    else
    {
        if (id == SettingId::NetPreset)
        {
            return context.mt_use_preset;
        }
        if (id == SettingId::NetBw || id == SettingId::NetSf || id == SettingId::NetCr)
        {
            return !context.mt_use_preset;
        }
    }

    return true;
}

} // namespace settings::ui::spec
