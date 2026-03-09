#include "platform/esp/arduino_common/app_config_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"

namespace app
{

namespace
{
bool applyRegionPrefFallback(app::AppConfig& config, Preferences& prefs)
{
    bool has_region = false;
    bool has_mc_preset = false;
    if (prefs.begin("chat", true))
    {
        has_region = prefs.isKey("region");
        has_mc_preset = prefs.isKey("mc_region_preset");
        prefs.end();
    }

    if (has_region && has_mc_preset)
    {
        return false;
    }

    int ui_region = -1;
    int ui_mc_preset = -1;
    if (prefs.begin("settings", true))
    {
        if (!has_region)
        {
            ui_region = prefs.getInt("chat_region", -1);
        }
        if (!has_mc_preset)
        {
            ui_mc_preset = prefs.getInt("mc_region_preset", -1);
        }
        prefs.end();
    }

    bool changed = false;
    if (!has_region && ui_region > 0)
    {
        const auto* region = chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(ui_region));
        if (region && region->code != meshtastic_Config_LoRaConfig_RegionCode_UNSET)
        {
            config.meshtastic_config.region = static_cast<uint8_t>(ui_region);
            changed = true;
        }
    }

    if (!has_mc_preset && ui_mc_preset >= 0)
    {
        const uint8_t preset_id = static_cast<uint8_t>(ui_mc_preset);
        if (chat::meshcore::isValidRegionPresetId(preset_id))
        {
            config.meshcore_config.meshcore_region_preset = preset_id;
            if (preset_id > 0)
            {
                if (const chat::meshcore::RegionPreset* preset =
                        chat::meshcore::findRegionPresetById(preset_id))
                {
                    config.meshcore_config.meshcore_freq_mhz = preset->freq_mhz;
                    config.meshcore_config.meshcore_bw_khz = preset->bw_khz;
                    config.meshcore_config.meshcore_sf = preset->sf;
                    config.meshcore_config.meshcore_cr = preset->cr;
                }
            }
            changed = true;
        }
    }

    return changed;
}
} // namespace

bool loadAppConfigFromPreferences(AppConfig& config, Preferences& prefs)
{
    auto& chat_policy = config.chat_policy;
    auto& meshtastic_config = config.meshtastic_config;
    auto& meshcore_config = config.meshcore_config;
    auto& mesh_protocol = config.mesh_protocol;
    auto& node_name = config.node_name;
    auto& short_name = config.short_name;
    auto& primary_enabled = config.primary_enabled;
    auto& secondary_enabled = config.secondary_enabled;
    auto& primary_uplink_enabled = config.primary_uplink_enabled;
    auto& primary_downlink_enabled = config.primary_downlink_enabled;
    auto& secondary_uplink_enabled = config.secondary_uplink_enabled;
    auto& secondary_downlink_enabled = config.secondary_downlink_enabled;
    auto& secondary_key = config.secondary_key;
    auto& gps_interval_ms = config.gps_interval_ms;
    auto& gps_mode = config.gps_mode;
    auto& gps_sat_mask = config.gps_sat_mask;
    auto& gps_strategy = config.gps_strategy;
    auto& gps_alt_ref = config.gps_alt_ref;
    auto& gps_coord_format = config.gps_coord_format;
    auto& motion_config = config.motion_config;
    auto& map_coord_system = config.map_coord_system;
    auto& map_source = config.map_source;
    auto& map_contour_enabled = config.map_contour_enabled;
    auto& map_track_enabled = config.map_track_enabled;
    auto& map_track_interval = config.map_track_interval;
    auto& map_track_format = config.map_track_format;
    auto& chat_channel = config.chat_channel;
    auto& net_duty_cycle = config.net_duty_cycle;
    auto& net_channel_util = config.net_channel_util;
    auto& privacy_encrypt_mode = config.privacy_encrypt_mode;
    auto& privacy_pki = config.privacy_pki;
    auto& privacy_nmea_output = config.privacy_nmea_output;
    auto& privacy_nmea_sentence = config.privacy_nmea_sentence;
    auto& route_enabled = config.route_enabled;
    auto& route_path = config.route_path;
    auto& aprs = config.aprs;

    prefs.begin("chat", true);

    // Load policy
    chat_policy.enable_relay = prefs.getBool("relay", true);
    chat_policy.hop_limit_default = prefs.getUChar("hop_limit", 2);
    chat_policy.ack_for_broadcast = prefs.getBool("ack_bcast", false);
    chat_policy.ack_for_squad = prefs.getBool("ack_squad", true);
    chat_policy.max_tx_retries = prefs.getUChar("max_retries", 1);

    // Load meshtastic config (legacy-compatible keys)
    meshtastic_config.region = prefs.getUChar("region", 0);
    if (meshtastic_config.region == 0)
    {
        meshtastic_config.region = AppConfig::kDefaultRegionCode;
    }
    meshtastic_config.modem_preset = prefs.getUChar("modem_preset", 0);
    meshtastic_config.tx_power = prefs.getChar("tx_power", 14);
    meshtastic_config.hop_limit = prefs.getUChar("mesh_hop_limit", 2);
    meshtastic_config.enable_relay = prefs.getBool("mesh_relay", true);
    meshtastic_config.use_preset = prefs.getBool("use_preset", true);
    meshtastic_config.bandwidth_khz = prefs.getFloat("mesh_bw_khz", meshtastic_config.bandwidth_khz);
    meshtastic_config.spread_factor = prefs.getUChar("mesh_sf", meshtastic_config.spread_factor);
    meshtastic_config.coding_rate = prefs.getUChar("mesh_cr", meshtastic_config.coding_rate);
    meshtastic_config.tx_enabled = prefs.getBool("mesh_tx_en", true);
    meshtastic_config.override_duty_cycle = prefs.getBool("mesh_override_dc", false);
    meshtastic_config.channel_num = prefs.getUShort("mesh_ch_num", 0);
    meshtastic_config.frequency_offset_mhz = prefs.getFloat("mesh_freq_off", 0.0f);
    meshtastic_config.override_frequency_mhz = prefs.getFloat("mesh_freq_override", 0.0f);
    meshtastic_config.ignore_mqtt = prefs.getBool("mesh_ignore_mqtt", false);
    meshtastic_config.config_ok_to_mqtt = prefs.getBool("mesh_ok_to_mqtt", false);

    // Load meshcore profile (protocol-specific keys)
    meshcore_config.meshcore_freq_mhz = prefs.getFloat("mc_freq", meshcore_config.meshcore_freq_mhz);
    meshcore_config.meshcore_bw_khz = prefs.getFloat("mc_bw", meshcore_config.meshcore_bw_khz);
    meshcore_config.meshcore_sf = prefs.getUChar("mc_sf", meshcore_config.meshcore_sf);
    meshcore_config.meshcore_cr = prefs.getUChar("mc_cr", meshcore_config.meshcore_cr);
    meshcore_config.meshcore_region_preset = prefs.getUChar("mc_region_preset",
                                                            meshcore_config.meshcore_region_preset);
    meshcore_config.tx_power = prefs.getChar("mc_tx", meshcore_config.tx_power);
    meshcore_config.meshcore_client_repeat = prefs.getBool("mc_repeat", false);
    meshcore_config.meshcore_rx_delay_base = prefs.getFloat("mc_rx_delay", meshcore_config.meshcore_rx_delay_base);
    meshcore_config.meshcore_airtime_factor = prefs.getFloat("mc_airtime", meshcore_config.meshcore_airtime_factor);
    meshcore_config.meshcore_flood_max = prefs.getUChar("mc_flood_max", meshcore_config.meshcore_flood_max);
    meshcore_config.meshcore_multi_acks = prefs.getBool("mc_multi_acks", false);
    meshcore_config.meshcore_channel_slot = prefs.getUChar("mc_ch_slot", 0);
    meshcore_config.tx_enabled = prefs.getBool("mc_tx_en", meshcore_config.tx_enabled);
    String mc_name = prefs.getString("mc_ch_name", meshcore_config.meshcore_channel_name);
    strncpy(meshcore_config.meshcore_channel_name, mc_name.c_str(),
            sizeof(meshcore_config.meshcore_channel_name) - 1);
    meshcore_config.meshcore_channel_name[sizeof(meshcore_config.meshcore_channel_name) - 1] = '\0';
    prefs.getBytes("mc_ch_key", meshcore_config.secondary_key, sizeof(meshcore_config.secondary_key));

    uint8_t mesh_protocol_raw = prefs.getUChar("mesh_protocol", 0xFF);
    if (chat::infra::isValidMeshProtocolValue(mesh_protocol_raw))
    {
        mesh_protocol = chat::infra::meshProtocolFromRaw(mesh_protocol_raw);
    }
    else
    {
        int legacy_protocol = prefs.getInt("mesh_protocol",
                                           static_cast<int>(chat::MeshProtocol::Meshtastic));
        mesh_protocol = chat::infra::meshProtocolFromRaw(
            static_cast<uint8_t>(legacy_protocol),
            chat::MeshProtocol::Meshtastic);
    }

    // Load device name
    size_t len = prefs.getBytes("node_name", node_name, sizeof(node_name) - 1);
    if (len > 0)
    {
        node_name[len] = '\0';
    }
    len = prefs.getBytes("short_name", short_name, sizeof(short_name) - 1);
    if (len > 0)
    {
        short_name[len] = '\0';
    }

    // Load channel settings
    primary_enabled = prefs.getBool("primary_enabled", true);
    secondary_enabled = prefs.getBool("secondary_enabled", false);
    primary_uplink_enabled = prefs.getBool("primary_uplink", false);
    primary_downlink_enabled = prefs.getBool("primary_downlink", false);
    secondary_uplink_enabled = prefs.getBool("secondary_uplink", false);
    secondary_downlink_enabled = prefs.getBool("secondary_downlink", false);
    prefs.getBytes("secondary_key", secondary_key, 16);
    memcpy(meshtastic_config.secondary_key, secondary_key, sizeof(meshtastic_config.secondary_key));

    auto clamp_tx_power = [](int8_t value) -> int8_t
    {
        if (value < AppConfig::kTxPowerMinDbm) return AppConfig::kTxPowerMinDbm;
        if (value > AppConfig::kTxPowerMaxDbm) return AppConfig::kTxPowerMaxDbm;
        return value;
    };
    meshtastic_config.tx_power = clamp_tx_power(meshtastic_config.tx_power);
    meshcore_config.tx_power = clamp_tx_power(meshcore_config.tx_power);

    prefs.end();

    prefs.begin("gps", true);
    gps_interval_ms = prefs.getUInt("gps_interval", gps_interval_ms);
    gps_mode = prefs.getUChar("gps_mode", gps_mode);
    gps_sat_mask = prefs.getUChar("gps_sat_mask", gps_sat_mask);
    gps_strategy = prefs.getUChar("gps_strategy", gps_strategy);
    gps_alt_ref = prefs.getUChar("gps_alt_ref", gps_alt_ref);
    gps_coord_format = prefs.getUChar("gps_coord_fmt", gps_coord_format);
    motion_config.idle_timeout_ms = prefs.getUInt("motion_idle_ms", motion_config.idle_timeout_ms);
    motion_config.sensor_id = prefs.getUChar("motion_sensor_id", motion_config.sensor_id);
    prefs.end();

    prefs.begin("settings", true);
    map_coord_system = prefs.getUChar("map_coord", map_coord_system);
    map_source = prefs.getUChar("map_source", map_source);
    if (map_source > 2)
    {
        map_source = 0;
    }
    map_contour_enabled = prefs.getBool("map_contour", map_contour_enabled);
    map_track_enabled = prefs.getBool("map_track", map_track_enabled);
    map_track_interval = prefs.getUChar("map_track_interval", map_track_interval);
    map_track_format = prefs.getUChar("map_track_format", map_track_format);
    chat_channel = prefs.getUChar("chat_channel", chat_channel);
    net_duty_cycle = prefs.getBool("net_duty_cycle", net_duty_cycle);
    net_channel_util = prefs.getUChar("net_util", net_channel_util);
    privacy_encrypt_mode = prefs.getUChar("privacy_encrypt", privacy_encrypt_mode);
    privacy_pki = prefs.getBool("privacy_pki", privacy_pki);
    privacy_nmea_output = prefs.getUChar("privacy_nmea", privacy_nmea_output);
    privacy_nmea_sentence = prefs.getUChar("privacy_nmea_sent", privacy_nmea_sentence);
    route_enabled = prefs.getBool("route_enabled", route_enabled);
    if (prefs.isKey("route_path"))
    {
        String path = prefs.getString("route_path", "");
        strncpy(route_path, path.c_str(), sizeof(route_path) - 1);
        route_path[sizeof(route_path) - 1] = '\0';
    }
    prefs.end();

    prefs.begin("aprs", true);
    aprs.enabled = prefs.getBool("enabled", aprs.enabled);
    if (prefs.isKey("igate_call"))
    {
        String call = prefs.getString("igate_call", "");
        strncpy(aprs.igate_callsign, call.c_str(), sizeof(aprs.igate_callsign) - 1);
        aprs.igate_callsign[sizeof(aprs.igate_callsign) - 1] = '\0';
    }
    aprs.igate_ssid = prefs.getUChar("igate_ssid", aprs.igate_ssid);
    if (prefs.isKey("to_call"))
    {
        String tocall = prefs.getString("to_call", "");
        strncpy(aprs.tocall, tocall.c_str(), sizeof(aprs.tocall) - 1);
        aprs.tocall[sizeof(aprs.tocall) - 1] = '\0';
    }
    if (prefs.isKey("path"))
    {
        String path = prefs.getString("path", "");
        strncpy(aprs.path, path.c_str(), sizeof(aprs.path) - 1);
        aprs.path[sizeof(aprs.path) - 1] = '\0';
    }
    aprs.tx_min_interval_s = prefs.getUShort("tx_min", aprs.tx_min_interval_s);
    aprs.dedupe_window_s = prefs.getUShort("dedupe", aprs.dedupe_window_s);
    aprs.symbol_table = static_cast<char>(prefs.getChar("sym_tab", aprs.symbol_table));
    aprs.symbol_code = static_cast<char>(prefs.getChar("sym_code", aprs.symbol_code));
    aprs.position_interval_s = prefs.getUShort("pos_interval", aprs.position_interval_s);
    size_t map_len = prefs.getBytes("node_map", aprs.node_map, sizeof(aprs.node_map));
    if (map_len > sizeof(aprs.node_map))
    {
        map_len = sizeof(aprs.node_map);
    }
    aprs.node_map_len = static_cast<uint8_t>(map_len);
    aprs.self_enable = prefs.getBool("self_enable", aprs.self_enable);
    if (prefs.isKey("self_call"))
    {
        String self_call = prefs.getString("self_call", "");
        strncpy(aprs.self_callsign, self_call.c_str(), sizeof(aprs.self_callsign) - 1);
        aprs.self_callsign[sizeof(aprs.self_callsign) - 1] = '\0';
    }
    prefs.end();
    if (applyRegionPrefFallback(config, prefs))
    {
        saveAppConfigToPreferences(config, prefs);
    }
    return true;
}

bool saveAppConfigToPreferences(AppConfig& config, Preferences& prefs)
{
    auto& chat_policy = config.chat_policy;
    auto& meshtastic_config = config.meshtastic_config;
    auto& meshcore_config = config.meshcore_config;
    auto& mesh_protocol = config.mesh_protocol;
    auto& node_name = config.node_name;
    auto& short_name = config.short_name;
    auto& primary_enabled = config.primary_enabled;
    auto& secondary_enabled = config.secondary_enabled;
    auto& primary_uplink_enabled = config.primary_uplink_enabled;
    auto& primary_downlink_enabled = config.primary_downlink_enabled;
    auto& secondary_uplink_enabled = config.secondary_uplink_enabled;
    auto& secondary_downlink_enabled = config.secondary_downlink_enabled;
    auto& secondary_key = config.secondary_key;
    auto& gps_interval_ms = config.gps_interval_ms;
    auto& gps_mode = config.gps_mode;
    auto& gps_sat_mask = config.gps_sat_mask;
    auto& gps_strategy = config.gps_strategy;
    auto& gps_alt_ref = config.gps_alt_ref;
    auto& gps_coord_format = config.gps_coord_format;
    auto& motion_config = config.motion_config;
    auto& map_coord_system = config.map_coord_system;
    auto& map_source = config.map_source;
    auto& map_contour_enabled = config.map_contour_enabled;
    auto& map_track_enabled = config.map_track_enabled;
    auto& map_track_interval = config.map_track_interval;
    auto& map_track_format = config.map_track_format;
    auto& chat_channel = config.chat_channel;
    auto& net_duty_cycle = config.net_duty_cycle;
    auto& net_channel_util = config.net_channel_util;
    auto& privacy_encrypt_mode = config.privacy_encrypt_mode;
    auto& privacy_pki = config.privacy_pki;
    auto& privacy_nmea_output = config.privacy_nmea_output;
    auto& privacy_nmea_sentence = config.privacy_nmea_sentence;
    auto& route_enabled = config.route_enabled;
    auto& route_path = config.route_path;
    auto& aprs = config.aprs;

    prefs.begin("chat", false);

    // Save policy
    prefs.putBool("relay", chat_policy.enable_relay);
    prefs.putUChar("hop_limit", chat_policy.hop_limit_default);
    prefs.putBool("ack_bcast", chat_policy.ack_for_broadcast);
    prefs.putBool("ack_squad", chat_policy.ack_for_squad);
    prefs.putUChar("max_retries", chat_policy.max_tx_retries);

    // Save meshtastic profile (legacy-compatible keys)
    prefs.putUChar("region", meshtastic_config.region);
    prefs.putUChar("modem_preset", meshtastic_config.modem_preset);
    prefs.putChar("tx_power", meshtastic_config.tx_power);
    prefs.putUChar("mesh_hop_limit", meshtastic_config.hop_limit);
    prefs.putBool("mesh_relay", meshtastic_config.enable_relay);
    prefs.putBool("use_preset", meshtastic_config.use_preset);
    prefs.putFloat("mesh_bw_khz", meshtastic_config.bandwidth_khz);
    prefs.putUChar("mesh_sf", meshtastic_config.spread_factor);
    prefs.putUChar("mesh_cr", meshtastic_config.coding_rate);
    prefs.putBool("mesh_tx_en", meshtastic_config.tx_enabled);
    prefs.putBool("mesh_override_dc", meshtastic_config.override_duty_cycle);
    prefs.putUShort("mesh_ch_num", meshtastic_config.channel_num);
    prefs.putFloat("mesh_freq_off", meshtastic_config.frequency_offset_mhz);
    prefs.putFloat("mesh_freq_override", meshtastic_config.override_frequency_mhz);
    prefs.putBool("mesh_ignore_mqtt", meshtastic_config.ignore_mqtt);
    prefs.putBool("mesh_ok_to_mqtt", meshtastic_config.config_ok_to_mqtt);

    // Save meshcore profile
    prefs.putFloat("mc_freq", meshcore_config.meshcore_freq_mhz);
    prefs.putFloat("mc_bw", meshcore_config.meshcore_bw_khz);
    prefs.putUChar("mc_sf", meshcore_config.meshcore_sf);
    prefs.putUChar("mc_cr", meshcore_config.meshcore_cr);
    prefs.putUChar("mc_region_preset", meshcore_config.meshcore_region_preset);
    prefs.putChar("mc_tx", meshcore_config.tx_power);
    prefs.putBool("mc_repeat", meshcore_config.meshcore_client_repeat);
    prefs.putFloat("mc_rx_delay", meshcore_config.meshcore_rx_delay_base);
    prefs.putFloat("mc_airtime", meshcore_config.meshcore_airtime_factor);
    prefs.putUChar("mc_flood_max", meshcore_config.meshcore_flood_max);
    prefs.putBool("mc_multi_acks", meshcore_config.meshcore_multi_acks);
    prefs.putUChar("mc_ch_slot", meshcore_config.meshcore_channel_slot);
    prefs.putBool("mc_tx_en", meshcore_config.tx_enabled);
    prefs.putString("mc_ch_name", meshcore_config.meshcore_channel_name);
    prefs.putBytes("mc_ch_key", meshcore_config.secondary_key, sizeof(meshcore_config.secondary_key));
    // Remove first so legacy key type mismatches cannot block updating this value.
    prefs.remove("mesh_protocol");
    prefs.putUChar("mesh_protocol", static_cast<uint8_t>(mesh_protocol));

    // Save device name
    prefs.putBytes("node_name", node_name, strlen(node_name));
    prefs.putBytes("short_name", short_name, strlen(short_name));

    // Save channel settings
    prefs.putBool("primary_enabled", primary_enabled);
    prefs.putBool("secondary_enabled", secondary_enabled);
    prefs.putBool("primary_uplink", primary_uplink_enabled);
    prefs.putBool("primary_downlink", primary_downlink_enabled);
    prefs.putBool("secondary_uplink", secondary_uplink_enabled);
    prefs.putBool("secondary_downlink", secondary_downlink_enabled);
    memcpy(secondary_key, meshtastic_config.secondary_key, sizeof(secondary_key));
    prefs.putBytes("secondary_key", secondary_key, 16);

    prefs.end();

    prefs.begin("gps", false);
    prefs.putUInt("gps_interval", gps_interval_ms);
    prefs.putUChar("gps_mode", gps_mode);
    prefs.putUChar("gps_sat_mask", gps_sat_mask);
    prefs.putUChar("gps_strategy", gps_strategy);
    prefs.putUChar("gps_alt_ref", gps_alt_ref);
    prefs.putUChar("gps_coord_fmt", gps_coord_format);
    prefs.putUInt("motion_idle_ms", motion_config.idle_timeout_ms);
    prefs.putUChar("motion_sensor_id", motion_config.sensor_id);
    prefs.end();

    prefs.begin("settings", false);
    prefs.putUChar("map_coord", map_coord_system);
    prefs.putUChar("map_source", map_source);
    prefs.putBool("map_contour", map_contour_enabled);
    prefs.putBool("map_track", map_track_enabled);
    prefs.putUChar("map_track_interval", map_track_interval);
    prefs.putUChar("map_track_format", map_track_format);
    prefs.putUChar("chat_channel", chat_channel);
    prefs.putBool("net_duty_cycle", net_duty_cycle);
    prefs.putUChar("net_util", net_channel_util);
    prefs.putUChar("privacy_encrypt", privacy_encrypt_mode);
    prefs.putBool("privacy_pki", privacy_pki);
    prefs.putUChar("privacy_nmea", privacy_nmea_output);
    prefs.putUChar("privacy_nmea_sent", privacy_nmea_sentence);
    prefs.putBool("route_enabled", route_enabled);
    prefs.putString("route_path", route_path);
    prefs.end();

    prefs.begin("aprs", false);
    prefs.putBool("enabled", aprs.enabled);
    prefs.putString("igate_call", aprs.igate_callsign);
    prefs.putUChar("igate_ssid", aprs.igate_ssid);
    prefs.putString("to_call", aprs.tocall);
    prefs.putString("path", aprs.path);
    prefs.putUShort("tx_min", aprs.tx_min_interval_s);
    prefs.putUShort("dedupe", aprs.dedupe_window_s);
    prefs.putChar("sym_tab", aprs.symbol_table);
    prefs.putChar("sym_code", aprs.symbol_code);
    prefs.putUShort("pos_interval", aprs.position_interval_s);
    if (aprs.node_map_len > 0)
    {
        prefs.putBytes("node_map", aprs.node_map, aprs.node_map_len);
    }
    else
    {
        prefs.remove("node_map");
    }
    prefs.putBool("self_enable", aprs.self_enable);
    prefs.putString("self_call", aprs.self_callsign);
    prefs.end();
    return true;
}

uint8_t loadMessageToneVolume()
{
    Preferences prefs;
    if (!prefs.begin("settings", true))
    {
        return 45;
    }
    int value = prefs.getInt("speaker_volume", 45);
    prefs.end();
    if (value < 0)
    {
        value = 0;
    }
    if (value > 100)
    {
        value = 100;
    }
    return static_cast<uint8_t>(value);
}

bool loadAppConfig(AppConfig& config)
{
    Preferences prefs;
    return loadAppConfigFromPreferences(config, prefs);
}

bool saveAppConfig(const AppConfig& config)
{
    Preferences prefs;
    AppConfig mutable_config = config;
    return saveAppConfigToPreferences(mutable_config, prefs);
}

} // namespace app
