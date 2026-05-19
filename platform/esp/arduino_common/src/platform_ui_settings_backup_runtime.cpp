#include "platform/ui/settings_backup_runtime.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "cJSON.h"
#include "chat/domain/chat_types.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/settings_store.h"

namespace platform::ui::settings_backup
{
namespace
{

constexpr const char* kBackupDir = "/trailmate";
constexpr const char* kBackupPath = "/trailmate/settings-backup.json";
constexpr const char* kBackupTempPath = "/trailmate/settings-backup.tmp";
constexpr const char* kBackupMagic = "trail-mate-settings-backup";
constexpr int kBackupVersion = 1;
constexpr std::size_t kMaxBackupBytes = 24 * 1024;

enum class ValueType : uint8_t
{
    Bool,
    Int,
    UInt,
    String,
    Blob,
};

struct ExtraKey
{
    const char* ns;
    const char* key;
    const char* storage_key;
    ValueType type;
};

constexpr ExtraKey kExtraKeys[] = {
    {"settings", "screen_timeout", "screen_timeout", ValueType::UInt},
    {"settings", "screen_brightness", "screen_bright", ValueType::Int},
    {"settings", "speaker_volume", "speaker_volume", ValueType::Int},
    {"settings", "vibration_enabled", "vibe_enabled", ValueType::Bool},
    {"settings", "display_locale", "disp_locale", ValueType::String},
    {"settings", "enabled_imes", "enabled_imes", ValueType::String},
    {"settings", "timezone_offset", "timezone_offset", ValueType::Int},
    {"settings", "chat_message_alerts", "chat_msg_alert", ValueType::Int},
    {"settings", "chat_contact_alerts", "chat_ct_alert", ValueType::Int},
    {"settings", "adv_debug", "adv_debug", ValueType::Bool},
    {"power", "gauge_design_mah", "gauge_dsgn", ValueType::UInt},
    {"power", "gauge_full_mah", "gauge_full_mah", ValueType::UInt},
};

void copy_bounded(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status_message(Status& out, const char* message, const char* detail = nullptr)
{
    copy_bounded(out.message, sizeof(out.message), message);
    copy_bounded(out.detail, sizeof(out.detail), detail);
}

bool sd_available()
{
    return ::platform::ui::device::card_ready() && SD.cardType() != CARD_NONE;
}

bool ensure_backup_dir()
{
    if (SD.exists(kBackupDir))
    {
        File dir = SD.open(kBackupDir, FILE_READ);
        const bool ok = static_cast<bool>(dir) && dir.isDirectory();
        if (dir)
        {
            dir.close();
        }
        return ok;
    }
    return SD.mkdir(kBackupDir);
}

const char* value_type_name(ValueType type)
{
    switch (type)
    {
    case ValueType::Bool:
        return "bool";
    case ValueType::Int:
        return "int";
    case ValueType::UInt:
        return "uint";
    case ValueType::String:
        return "string";
    case ValueType::Blob:
        return "blob";
    }
    return "unknown";
}

bool bytes_to_hex(const uint8_t* data, std::size_t len, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (!data && len > 0)
    {
        return false;
    }
    if (out_len < len * 2 + 1)
    {
        return false;
    }
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (std::size_t index = 0; index < len; ++index)
    {
        out[index * 2] = kHex[(data[index] >> 4) & 0x0F];
        out[index * 2 + 1] = kHex[data[index] & 0x0F];
    }
    out[len * 2] = '\0';
    return true;
}

int hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return 10 + ch - 'A';
    }
    return -1;
}

bool hex_to_bytes(const char* text, std::vector<uint8_t>& out)
{
    out.clear();
    if (!text)
    {
        return false;
    }
    const std::size_t len = std::strlen(text);
    if ((len % 2) != 0)
    {
        return false;
    }
    out.resize(len / 2);
    for (std::size_t index = 0; index < out.size(); ++index)
    {
        const int high = hex_nibble(text[index * 2]);
        const int low = hex_nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            out.clear();
            return false;
        }
        out[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

cJSON* add_object(cJSON* parent, const char* key)
{
    cJSON* object = cJSON_AddObjectToObject(parent, key);
    return object;
}

void add_bool(cJSON* parent, const char* key, bool value)
{
    cJSON_AddBoolToObject(parent, key, value);
}

void add_int(cJSON* parent, const char* key, int value)
{
    cJSON_AddNumberToObject(parent, key, value);
}

void add_uint(cJSON* parent, const char* key, uint32_t value)
{
    cJSON_AddNumberToObject(parent, key, static_cast<double>(value));
}

void add_float(cJSON* parent, const char* key, float value)
{
    cJSON_AddNumberToObject(parent, key, static_cast<double>(value));
}

void add_string(cJSON* parent, const char* key, const char* value)
{
    cJSON_AddStringToObject(parent, key, value ? value : "");
}

void add_blob_hex(cJSON* parent, const char* key, const uint8_t* data, std::size_t len)
{
    char hex[chat::kMeshtasticChannelKeyMaxLen * 2 + 1] = {};
    if (bytes_to_hex(data, len, hex, sizeof(hex)))
    {
        add_string(parent, key, hex);
    }
}

bool json_bool(cJSON* object, const char* key, bool fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsBool(item))
    {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

int json_int(cJSON* object, const char* key, int fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item))
    {
        return fallback;
    }
    return static_cast<int>(item->valuedouble);
}

uint32_t json_uint(cJSON* object, const char* key, uint32_t fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0)
    {
        return fallback;
    }
    return static_cast<uint32_t>(item->valuedouble);
}

float json_float(cJSON* object, const char* key, float fallback)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble))
    {
        return fallback;
    }
    return static_cast<float>(item->valuedouble);
}

const char* json_string(cJSON* object, const char* key)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : nullptr;
}

void copy_json_string(cJSON* object, const char* key, char* out, std::size_t out_len)
{
    const char* value = json_string(object, key);
    if (value)
    {
        copy_bounded(out, out_len, value);
    }
}

void copy_json_blob(cJSON* object,
                    const char* key,
                    uint8_t* out,
                    std::size_t capacity,
                    uint8_t* out_len = nullptr)
{
    const char* hex = json_string(object, key);
    if (!hex)
    {
        return;
    }
    std::vector<uint8_t> bytes;
    if (!hex_to_bytes(hex, bytes) || bytes.size() > capacity)
    {
        return;
    }
    std::memset(out, 0, capacity);
    if (!bytes.empty())
    {
        std::memcpy(out, bytes.data(), bytes.size());
    }
    if (out_len)
    {
        *out_len = static_cast<uint8_t>(bytes.size());
    }
}

void add_chat_policy(cJSON* parent, const chat::ChatPolicy& policy)
{
    cJSON* object = add_object(parent, "chat_policy");
    if (!object)
    {
        return;
    }
    add_bool(object, "enable_relay", policy.enable_relay);
    add_int(object, "hop_limit_default", policy.hop_limit_default);
    add_bool(object, "ack_for_broadcast", policy.ack_for_broadcast);
    add_bool(object, "ack_for_squad", policy.ack_for_squad);
    add_int(object, "max_tx_retries", policy.max_tx_retries);
    add_int(object, "max_channels", policy.max_channels);
}

void restore_chat_policy(cJSON* object, chat::ChatPolicy& policy)
{
    if (!cJSON_IsObject(object))
    {
        return;
    }
    policy.enable_relay = json_bool(object, "enable_relay", policy.enable_relay);
    policy.hop_limit_default = static_cast<uint8_t>(
        json_int(object, "hop_limit_default", policy.hop_limit_default));
    policy.ack_for_broadcast = json_bool(object, "ack_for_broadcast", policy.ack_for_broadcast);
    policy.ack_for_squad = json_bool(object, "ack_for_squad", policy.ack_for_squad);
    policy.max_tx_retries = static_cast<uint8_t>(
        json_int(object, "max_tx_retries", policy.max_tx_retries));
    policy.max_channels = static_cast<uint8_t>(
        json_int(object, "max_channels", policy.max_channels));
}

void add_mesh_config(cJSON* parent,
                     const char* key,
                     const chat::MeshConfig& config,
                     bool include_meshcore_fields)
{
    cJSON* object = add_object(parent, key);
    if (!object)
    {
        return;
    }
    add_int(object, "region", config.region);
    add_bool(object, "use_preset", config.use_preset);
    add_int(object, "modem_preset", config.modem_preset);
    add_float(object, "bandwidth_khz", config.bandwidth_khz);
    add_int(object, "spread_factor", config.spread_factor);
    add_int(object, "coding_rate", config.coding_rate);
    add_int(object, "tx_power", config.tx_power);
    add_int(object, "hop_limit", config.hop_limit);
    add_bool(object, "tx_enabled", config.tx_enabled);
    add_bool(object, "override_duty_cycle", config.override_duty_cycle);
    add_int(object, "channel_num", config.channel_num);
    add_float(object, "frequency_offset_mhz", config.frequency_offset_mhz);
    add_float(object, "override_frequency_mhz", config.override_frequency_mhz);
    add_bool(object, "enable_relay", config.enable_relay);
    add_bool(object, "ignore_mqtt", config.ignore_mqtt);
    add_bool(object, "config_ok_to_mqtt", config.config_ok_to_mqtt);
    add_string(object, "primary_channel_name", config.primary_channel_name);
    add_string(object, "secondary_channel_name", config.secondary_channel_name);
    add_uint(object, "primary_channel_id", config.primary_channel_id);
    add_uint(object, "secondary_channel_id", config.secondary_channel_id);
    add_int(object, "primary_key_len", config.primary_key_len);
    add_blob_hex(object, "primary_key", config.primary_key, config.primary_key_len);
    add_int(object, "secondary_key_len", config.secondary_key_len);
    add_blob_hex(object, "secondary_key", config.secondary_key, config.secondary_key_len);
    if (include_meshcore_fields)
    {
        add_int(object, "meshcore_region_preset", config.meshcore_region_preset);
        add_float(object, "meshcore_freq_mhz", config.meshcore_freq_mhz);
        add_float(object, "meshcore_bw_khz", config.meshcore_bw_khz);
        add_int(object, "meshcore_sf", config.meshcore_sf);
        add_int(object, "meshcore_cr", config.meshcore_cr);
        add_bool(object, "meshcore_client_repeat", config.meshcore_client_repeat);
        add_float(object, "meshcore_rx_delay_base", config.meshcore_rx_delay_base);
        add_float(object, "meshcore_airtime_factor", config.meshcore_airtime_factor);
        add_int(object, "meshcore_flood_max", config.meshcore_flood_max);
        add_bool(object, "meshcore_multi_acks", config.meshcore_multi_acks);
        add_int(object, "meshcore_channel_slot", config.meshcore_channel_slot);
        add_string(object, "meshcore_channel_name", config.meshcore_channel_name);
        add_blob_hex(object, "meshcore_channel_key", config.secondary_key, chat::kMeshCoreChannelKeyLen);
    }
}

void restore_mesh_config(cJSON* object,
                         chat::MeshConfig& config,
                         bool include_meshcore_fields)
{
    if (!cJSON_IsObject(object))
    {
        return;
    }
    config.region = static_cast<uint8_t>(json_int(object, "region", config.region));
    config.use_preset = json_bool(object, "use_preset", config.use_preset);
    config.modem_preset = static_cast<uint8_t>(json_int(object, "modem_preset", config.modem_preset));
    config.bandwidth_khz = json_float(object, "bandwidth_khz", config.bandwidth_khz);
    config.spread_factor = static_cast<uint8_t>(json_int(object, "spread_factor", config.spread_factor));
    config.coding_rate = static_cast<uint8_t>(json_int(object, "coding_rate", config.coding_rate));
    config.tx_power = static_cast<int8_t>(json_int(object, "tx_power", config.tx_power));
    config.hop_limit = static_cast<uint8_t>(json_int(object, "hop_limit", config.hop_limit));
    config.tx_enabled = json_bool(object, "tx_enabled", config.tx_enabled);
    config.override_duty_cycle = json_bool(object, "override_duty_cycle", config.override_duty_cycle);
    config.channel_num = static_cast<uint16_t>(json_int(object, "channel_num", config.channel_num));
    config.frequency_offset_mhz = json_float(object, "frequency_offset_mhz", config.frequency_offset_mhz);
    config.override_frequency_mhz = json_float(object, "override_frequency_mhz", config.override_frequency_mhz);
    config.enable_relay = json_bool(object, "enable_relay", config.enable_relay);
    config.ignore_mqtt = json_bool(object, "ignore_mqtt", config.ignore_mqtt);
    config.config_ok_to_mqtt = json_bool(object, "config_ok_to_mqtt", config.config_ok_to_mqtt);
    copy_json_string(object, "primary_channel_name", config.primary_channel_name, sizeof(config.primary_channel_name));
    copy_json_string(object, "secondary_channel_name", config.secondary_channel_name, sizeof(config.secondary_channel_name));
    config.primary_channel_id = json_uint(object, "primary_channel_id", config.primary_channel_id);
    config.secondary_channel_id = json_uint(object, "secondary_channel_id", config.secondary_channel_id);
    uint8_t primary_key_len = static_cast<uint8_t>(
        json_int(object, "primary_key_len", config.primary_key_len));
    copy_json_blob(object, "primary_key", config.primary_key, sizeof(config.primary_key), &primary_key_len);
    config.primary_key_len = chat::normalizeMeshtasticChannelKeyLen(
        config.primary_key, sizeof(config.primary_key), primary_key_len);
    uint8_t secondary_key_len = static_cast<uint8_t>(
        json_int(object, "secondary_key_len", config.secondary_key_len));
    copy_json_blob(object, "secondary_key", config.secondary_key, sizeof(config.secondary_key), &secondary_key_len);
    config.secondary_key_len = chat::normalizeMeshtasticChannelKeyLen(
        config.secondary_key, sizeof(config.secondary_key), secondary_key_len);
    if (include_meshcore_fields)
    {
        config.meshcore_region_preset = static_cast<uint8_t>(
            json_int(object, "meshcore_region_preset", config.meshcore_region_preset));
        config.meshcore_freq_mhz = json_float(object, "meshcore_freq_mhz", config.meshcore_freq_mhz);
        config.meshcore_bw_khz = json_float(object, "meshcore_bw_khz", config.meshcore_bw_khz);
        config.meshcore_sf = static_cast<uint8_t>(json_int(object, "meshcore_sf", config.meshcore_sf));
        config.meshcore_cr = static_cast<uint8_t>(json_int(object, "meshcore_cr", config.meshcore_cr));
        config.meshcore_client_repeat = json_bool(object, "meshcore_client_repeat", config.meshcore_client_repeat);
        config.meshcore_rx_delay_base = json_float(object, "meshcore_rx_delay_base", config.meshcore_rx_delay_base);
        config.meshcore_airtime_factor = json_float(object, "meshcore_airtime_factor", config.meshcore_airtime_factor);
        config.meshcore_flood_max = static_cast<uint8_t>(json_int(object, "meshcore_flood_max", config.meshcore_flood_max));
        config.meshcore_multi_acks = json_bool(object, "meshcore_multi_acks", config.meshcore_multi_acks);
        config.meshcore_channel_slot = static_cast<uint8_t>(
            json_int(object, "meshcore_channel_slot", config.meshcore_channel_slot));
        copy_json_string(object, "meshcore_channel_name", config.meshcore_channel_name, sizeof(config.meshcore_channel_name));
        copy_json_blob(object, "meshcore_channel_key", config.secondary_key, sizeof(config.secondary_key));
    }
}

void add_aprs_config(cJSON* parent, const app::AprsConfig& config)
{
    cJSON* object = add_object(parent, "aprs");
    if (!object)
    {
        return;
    }
    add_bool(object, "enabled", config.enabled);
    add_string(object, "igate_callsign", config.igate_callsign);
    add_int(object, "igate_ssid", config.igate_ssid);
    add_string(object, "tocall", config.tocall);
    add_string(object, "path", config.path);
    add_int(object, "tx_min_interval_s", config.tx_min_interval_s);
    add_int(object, "dedupe_window_s", config.dedupe_window_s);
    char symbol_table[2] = {config.symbol_table, '\0'};
    char symbol_code[2] = {config.symbol_code, '\0'};
    add_string(object, "symbol_table", symbol_table);
    add_string(object, "symbol_code", symbol_code);
    add_int(object, "position_interval_s", config.position_interval_s);
    add_bool(object, "self_enable", config.self_enable);
    add_string(object, "self_callsign", config.self_callsign);
}

void restore_aprs_config(cJSON* object, app::AprsConfig& config)
{
    if (!cJSON_IsObject(object))
    {
        return;
    }
    config.enabled = json_bool(object, "enabled", config.enabled);
    copy_json_string(object, "igate_callsign", config.igate_callsign, sizeof(config.igate_callsign));
    config.igate_ssid = static_cast<uint8_t>(json_int(object, "igate_ssid", config.igate_ssid));
    copy_json_string(object, "tocall", config.tocall, sizeof(config.tocall));
    copy_json_string(object, "path", config.path, sizeof(config.path));
    config.tx_min_interval_s = static_cast<uint16_t>(
        json_int(object, "tx_min_interval_s", config.tx_min_interval_s));
    config.dedupe_window_s = static_cast<uint16_t>(
        json_int(object, "dedupe_window_s", config.dedupe_window_s));
    const char* symbol_table = json_string(object, "symbol_table");
    if (symbol_table && symbol_table[0] != '\0')
    {
        config.symbol_table = symbol_table[0];
    }
    const char* symbol_code = json_string(object, "symbol_code");
    if (symbol_code && symbol_code[0] != '\0')
    {
        config.symbol_code = symbol_code[0];
    }
    config.position_interval_s = static_cast<uint16_t>(
        json_int(object, "position_interval_s", config.position_interval_s));
    config.self_enable = json_bool(object, "self_enable", config.self_enable);
    copy_json_string(object, "self_callsign", config.self_callsign, sizeof(config.self_callsign));
}

cJSON* create_app_config_json(const app::AppConfig& config)
{
    cJSON* object = cJSON_CreateObject();
    if (!object)
    {
        return nullptr;
    }
    add_chat_policy(object, config.chat_policy);
    add_mesh_config(object, "meshtastic", config.meshtastic_config, false);
    add_mesh_config(object, "meshcore", config.meshcore_config, true);
    add_mesh_config(object, "rnode", config.rnode_config, false);
    add_int(object, "mesh_protocol", static_cast<int>(config.mesh_protocol));
    add_string(object, "node_name", config.node_name);
    add_string(object, "short_name", config.short_name);
    add_bool(object, "ble_enabled", config.ble_enabled);
    add_bool(object, "primary_enabled", config.primary_enabled);
    add_bool(object, "secondary_enabled", config.secondary_enabled);
    add_bool(object, "primary_uplink_enabled", config.primary_uplink_enabled);
    add_bool(object, "primary_downlink_enabled", config.primary_downlink_enabled);
    add_bool(object, "secondary_uplink_enabled", config.secondary_uplink_enabled);
    add_bool(object, "secondary_downlink_enabled", config.secondary_downlink_enabled);
    add_bool(object, "gps_enabled", config.gps_enabled);
    add_uint(object, "gps_init_baud", config.gps_init_baud);
    add_uint(object, "gps_init_probe_ms", config.gps_init_probe_ms);
    add_int(object, "gps_init_profile", config.gps_init_profile);
    add_int(object, "gps_init_rxm_policy", config.gps_init_rxm_policy);
    add_int(object, "gps_init_gnss_policy", config.gps_init_gnss_policy);
    add_int(object, "gps_init_nmea_policy", config.gps_init_nmea_policy);
    add_uint(object, "gps_interval_ms", config.gps_interval_ms);
    add_int(object, "gps_mode", config.gps_mode);
    add_int(object, "gps_sat_mask", config.gps_sat_mask);
    add_int(object, "gps_strategy", config.gps_strategy);
    add_int(object, "gps_alt_ref", config.gps_alt_ref);
    add_int(object, "gps_coord_format", config.gps_coord_format);
    add_uint(object, "motion_idle_ms", config.motion_config.idle_timeout_ms);
    add_int(object, "motion_sensor_id", config.motion_config.sensor_id);
    add_int(object, "external_nmea_output_hz", config.external_nmea_output_hz);
    add_int(object, "external_nmea_sentence_mask", config.external_nmea_sentence_mask);
    add_int(object, "map_coord_system", config.map_coord_system);
    add_int(object, "map_source", config.map_source);
    add_bool(object, "map_contour_enabled", config.map_contour_enabled);
    add_bool(object, "map_track_enabled", config.map_track_enabled);
    add_int(object, "map_track_interval", config.map_track_interval);
    add_int(object, "map_track_format", config.map_track_format);
    add_int(object, "chat_channel", config.chat_channel);
    add_bool(object, "net_duty_cycle", config.net_duty_cycle);
    add_int(object, "net_channel_util", config.net_channel_util);
    add_int(object, "privacy_encrypt_mode", config.privacy_encrypt_mode);
    add_bool(object, "route_enabled", config.route_enabled);
    add_string(object, "route_path", config.route_path);
    add_aprs_config(object, config.aprs);
    return object;
}

void restore_app_config_json(cJSON* object, app::AppConfig& config)
{
    if (!cJSON_IsObject(object))
    {
        return;
    }
    restore_chat_policy(cJSON_GetObjectItemCaseSensitive(object, "chat_policy"), config.chat_policy);
    restore_mesh_config(cJSON_GetObjectItemCaseSensitive(object, "meshtastic"), config.meshtastic_config, false);
    restore_mesh_config(cJSON_GetObjectItemCaseSensitive(object, "meshcore"), config.meshcore_config, true);
    restore_mesh_config(cJSON_GetObjectItemCaseSensitive(object, "rnode"), config.rnode_config, false);
    const int protocol = json_int(object, "mesh_protocol", static_cast<int>(config.mesh_protocol));
    if (protocol >= static_cast<int>(chat::MeshProtocol::Meshtastic) &&
        protocol <= static_cast<int>(chat::MeshProtocol::LXMF))
    {
        config.mesh_protocol = static_cast<chat::MeshProtocol>(protocol);
    }
    copy_json_string(object, "node_name", config.node_name, sizeof(config.node_name));
    copy_json_string(object, "short_name", config.short_name, sizeof(config.short_name));
    config.ble_enabled = json_bool(object, "ble_enabled", config.ble_enabled);
    config.primary_enabled = json_bool(object, "primary_enabled", config.primary_enabled);
    config.secondary_enabled = json_bool(object, "secondary_enabled", config.secondary_enabled);
    config.primary_uplink_enabled = json_bool(object, "primary_uplink_enabled", config.primary_uplink_enabled);
    config.primary_downlink_enabled = json_bool(object, "primary_downlink_enabled", config.primary_downlink_enabled);
    config.secondary_uplink_enabled = json_bool(object, "secondary_uplink_enabled", config.secondary_uplink_enabled);
    config.secondary_downlink_enabled = json_bool(object, "secondary_downlink_enabled", config.secondary_downlink_enabled);
    config.gps_enabled = json_bool(object, "gps_enabled", config.gps_enabled);
    config.gps_init_baud = json_uint(object, "gps_init_baud", config.gps_init_baud);
    config.gps_init_probe_ms = json_uint(object, "gps_init_probe_ms", config.gps_init_probe_ms);
    config.gps_init_profile = static_cast<uint8_t>(json_int(object, "gps_init_profile", config.gps_init_profile));
    config.gps_init_rxm_policy = static_cast<uint8_t>(json_int(object, "gps_init_rxm_policy", config.gps_init_rxm_policy));
    config.gps_init_gnss_policy = static_cast<uint8_t>(json_int(object, "gps_init_gnss_policy", config.gps_init_gnss_policy));
    config.gps_init_nmea_policy = static_cast<uint8_t>(json_int(object, "gps_init_nmea_policy", config.gps_init_nmea_policy));
    config.gps_interval_ms = json_uint(object, "gps_interval_ms", config.gps_interval_ms);
    config.gps_mode = static_cast<uint8_t>(json_int(object, "gps_mode", config.gps_mode));
    config.gps_sat_mask = static_cast<uint8_t>(json_int(object, "gps_sat_mask", config.gps_sat_mask));
    config.gps_strategy = static_cast<uint8_t>(json_int(object, "gps_strategy", config.gps_strategy));
    config.gps_alt_ref = static_cast<uint8_t>(json_int(object, "gps_alt_ref", config.gps_alt_ref));
    config.gps_coord_format = static_cast<uint8_t>(json_int(object, "gps_coord_format", config.gps_coord_format));
    config.motion_config.idle_timeout_ms = json_uint(object, "motion_idle_ms", config.motion_config.idle_timeout_ms);
    config.motion_config.sensor_id = static_cast<uint8_t>(
        json_int(object, "motion_sensor_id", config.motion_config.sensor_id));
    config.external_nmea_output_hz = static_cast<uint8_t>(
        json_int(object, "external_nmea_output_hz", config.external_nmea_output_hz));
    config.external_nmea_sentence_mask = static_cast<uint8_t>(
        json_int(object, "external_nmea_sentence_mask", config.external_nmea_sentence_mask));
    config.map_coord_system = static_cast<uint8_t>(json_int(object, "map_coord_system", config.map_coord_system));
    config.map_source = static_cast<uint8_t>(json_int(object, "map_source", config.map_source));
    config.map_contour_enabled = json_bool(object, "map_contour_enabled", config.map_contour_enabled);
    config.map_track_enabled = json_bool(object, "map_track_enabled", config.map_track_enabled);
    config.map_track_interval = static_cast<uint8_t>(json_int(object, "map_track_interval", config.map_track_interval));
    config.map_track_format = static_cast<uint8_t>(json_int(object, "map_track_format", config.map_track_format));
    config.chat_channel = static_cast<uint8_t>(json_int(object, "chat_channel", config.chat_channel));
    config.net_duty_cycle = json_bool(object, "net_duty_cycle", config.net_duty_cycle);
    config.net_channel_util = static_cast<uint8_t>(json_int(object, "net_channel_util", config.net_channel_util));
    config.privacy_encrypt_mode = static_cast<uint8_t>(
        json_int(object, "privacy_encrypt_mode", config.privacy_encrypt_mode));
    config.route_enabled = json_bool(object, "route_enabled", config.route_enabled);
    copy_json_string(object, "route_path", config.route_path, sizeof(config.route_path));
    restore_aprs_config(cJSON_GetObjectItemCaseSensitive(object, "aprs"), config.aprs);
}

PreferenceType extra_preference_type(const ExtraKey& key)
{
    Preferences prefs;
    if (!prefs.begin(key.ns, true))
    {
        return PT_INVALID;
    }
    const PreferenceType type = prefs.getType(key.storage_key ? key.storage_key : key.key);
    prefs.end();
    return type;
}

void add_extra_value(cJSON* parent, const ExtraKey& key)
{
    const PreferenceType pref_type = extra_preference_type(key);
    if (pref_type == PT_INVALID)
    {
        return;
    }

    cJSON* ns_object = cJSON_GetObjectItemCaseSensitive(parent, key.ns);
    if (!cJSON_IsObject(ns_object))
    {
        ns_object = cJSON_AddObjectToObject(parent, key.ns);
    }
    if (!ns_object)
    {
        return;
    }

    cJSON* value_object = cJSON_AddObjectToObject(ns_object, key.key);
    if (!value_object)
    {
        return;
    }
    add_string(value_object, "type", value_type_name(key.type));

    switch (key.type)
    {
    case ValueType::Bool:
        add_bool(value_object,
                 "value",
                 ::platform::ui::settings_store::get_bool(key.ns, key.key, false));
        break;
    case ValueType::Int:
        add_int(value_object,
                "value",
                ::platform::ui::settings_store::get_int(key.ns, key.key, 0));
        break;
    case ValueType::UInt:
        add_uint(value_object,
                 "value",
                 ::platform::ui::settings_store::get_uint(key.ns, key.key, 0));
        break;
    case ValueType::String:
    {
        std::string value;
        if (::platform::ui::settings_store::get_string(key.ns, key.key, value))
        {
            add_string(value_object, "value", value.c_str());
        }
        break;
    }
    case ValueType::Blob:
    {
        std::vector<uint8_t> value;
        if (::platform::ui::settings_store::get_blob(key.ns, key.key, value))
        {
            std::string hex(value.size() * 2 + 1, '\0');
            if (bytes_to_hex(value.data(), value.size(), &hex[0], hex.size()))
            {
                add_string(value_object, "value", hex.c_str());
            }
        }
        break;
    }
    }
}

void restore_extra_value(const ExtraKey& key, cJSON* value_object)
{
    if (!cJSON_IsObject(value_object))
    {
        return;
    }

    cJSON* value = cJSON_GetObjectItemCaseSensitive(value_object, "value");
    switch (key.type)
    {
    case ValueType::Bool:
        if (cJSON_IsBool(value))
        {
            ::platform::ui::settings_store::put_bool(key.ns, key.key, cJSON_IsTrue(value));
        }
        break;
    case ValueType::Int:
        if (cJSON_IsNumber(value))
        {
            ::platform::ui::settings_store::put_int(key.ns, key.key, static_cast<int>(value->valuedouble));
        }
        break;
    case ValueType::UInt:
        if (cJSON_IsNumber(value) && value->valuedouble >= 0.0)
        {
            ::platform::ui::settings_store::put_uint(key.ns, key.key, static_cast<uint32_t>(value->valuedouble));
        }
        break;
    case ValueType::String:
        if (cJSON_IsString(value) && value->valuestring)
        {
            (void)::platform::ui::settings_store::put_string(key.ns, key.key, value->valuestring);
        }
        break;
    case ValueType::Blob:
        if (cJSON_IsString(value) && value->valuestring)
        {
            std::vector<uint8_t> bytes;
            if (hex_to_bytes(value->valuestring, bytes))
            {
                (void)::platform::ui::settings_store::put_blob(
                    key.ns,
                    key.key,
                    bytes.empty() ? nullptr : bytes.data(),
                    bytes.size());
            }
        }
        break;
    }
}

void add_extra_settings(cJSON* root)
{
    cJSON* extra = cJSON_AddObjectToObject(root, "extra_settings");
    if (!extra)
    {
        return;
    }
    for (const ExtraKey& key : kExtraKeys)
    {
        add_extra_value(extra, key);
    }
}

void restore_extra_settings(cJSON* root)
{
    cJSON* extra = cJSON_GetObjectItemCaseSensitive(root, "extra_settings");
    if (!cJSON_IsObject(extra))
    {
        return;
    }
    for (const ExtraKey& key : kExtraKeys)
    {
        cJSON* ns_object = cJSON_GetObjectItemCaseSensitive(extra, key.ns);
        cJSON* value_object = cJSON_IsObject(ns_object)
                                  ? cJSON_GetObjectItemCaseSensitive(ns_object, key.key)
                                  : nullptr;
        restore_extra_value(key, value_object);
    }
}

cJSON* create_backup_document()
{
    app::IAppFacade& facade = app::appFacade();
    cJSON* root = cJSON_CreateObject();
    if (!root)
    {
        return nullptr;
    }
    add_string(root, "magic", kBackupMagic);
    add_int(root, "version", kBackupVersion);
    add_string(root, "firmware", ::platform::ui::device::firmware_version());
    add_uint(root, "created_ms", millis());

    cJSON* app_config = create_app_config_json(facade.getConfig());
    if (!app_config)
    {
        cJSON_Delete(root);
        return nullptr;
    }
    cJSON_AddItemToObject(root, "app_config", app_config);
    add_extra_settings(root);
    return root;
}

bool write_text_atomic(const char* path, const char* temp_path, const char* text, std::size_t len)
{
    if (!path || !temp_path || !text)
    {
        return false;
    }
    if (SD.exists(temp_path))
    {
        SD.remove(temp_path);
    }
    File file = SD.open(temp_path, FILE_WRITE);
    if (!file)
    {
        return false;
    }
    const bool wrote = file.write(reinterpret_cast<const uint8_t*>(text), len) == len;
    file.close();
    if (!wrote)
    {
        SD.remove(temp_path);
        return false;
    }
    if (SD.exists(path))
    {
        SD.remove(path);
    }
    if (!SD.rename(temp_path, path))
    {
        SD.remove(temp_path);
        return false;
    }
    return true;
}

bool read_file_text(const char* path, std::string& out)
{
    out.clear();
    File file = SD.open(path, FILE_READ);
    if (!file)
    {
        return false;
    }
    const std::size_t size = file.size();
    if (size == 0 || size > kMaxBackupBytes)
    {
        file.close();
        return false;
    }
    out.resize(size);
    const std::size_t read = file.readBytes(&out[0], size);
    file.close();
    if (read != size)
    {
        out.clear();
        return false;
    }
    return true;
}

bool validate_document(cJSON* root, char* error, std::size_t error_len)
{
    if (!cJSON_IsObject(root))
    {
        copy_bounded(error, error_len, "Invalid backup document");
        return false;
    }
    const char* magic = json_string(root, "magic");
    if (!magic || std::strcmp(magic, kBackupMagic) != 0)
    {
        copy_bounded(error, error_len, "Not a Trail-Mate settings backup");
        return false;
    }
    const int version = json_int(root, "version", 0);
    if (version <= 0 || version > kBackupVersion)
    {
        copy_bounded(error, error_len, "Unsupported backup version");
        return false;
    }
    if (!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root, "app_config")))
    {
        copy_bounded(error, error_len, "Backup missing app_config");
        return false;
    }
    return true;
}

} // namespace

bool is_supported()
{
    return true;
}

Status status()
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_available();
    out.has_backup = out.sd_present && SD.exists(kBackupPath);
    out.busy = false;
    if (!out.sd_present)
    {
        set_status_message(out, "SD not detected", kBackupPath);
    }
    else if (out.has_backup)
    {
        set_status_message(out, "Backup found", kBackupPath);
    }
    else
    {
        set_status_message(out, "No backup found", kBackupPath);
    }
    return out;
}

bool backup()
{
    if (!sd_available())
    {
        return false;
    }
    if (!ensure_backup_dir())
    {
        return false;
    }
    cJSON* root = create_backup_document();
    if (!root)
    {
        return false;
    }
    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text)
    {
        return false;
    }
    const bool ok = write_text_atomic(kBackupPath, kBackupTempPath, text, std::strlen(text));
    cJSON_free(text);
    return ok;
}

bool restore()
{
    if (!sd_available() || !SD.exists(kBackupPath))
    {
        return false;
    }
    std::string text;
    if (!read_file_text(kBackupPath, text))
    {
        return false;
    }
    cJSON* root = cJSON_ParseWithLength(text.c_str(), text.size());
    char error[96] = {};
    if (!validate_document(root, error, sizeof(error)))
    {
        if (root)
        {
            cJSON_Delete(root);
        }
        std::printf("[SettingsBackup] restore validation failed: %s\n", error);
        return false;
    }

    app::AppConfig restored = app::appFacade().getConfig();
    restore_app_config_json(cJSON_GetObjectItemCaseSensitive(root, "app_config"), restored);
    restore_extra_settings(root);
    cJSON_Delete(root);

    app::IAppFacade& facade = app::appFacade();
    facade.getConfig() = restored;
    facade.saveConfig();
    return true;
}

bool remove()
{
    if (!sd_available())
    {
        return false;
    }
    if (SD.exists(kBackupTempPath))
    {
        SD.remove(kBackupTempPath);
    }
    return !SD.exists(kBackupPath) || SD.remove(kBackupPath);
}

} // namespace platform::ui::settings_backup
