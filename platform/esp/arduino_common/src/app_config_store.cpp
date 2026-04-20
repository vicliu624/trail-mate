#include "platform/esp/arduino_common/app_config_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include "platform/ui/settings_store.h"

namespace app
{

bool loadAppConfigFromPreferences(AppConfig& config,
                                  Preferences& prefs,
                                  bool emit_logs);
bool saveAppConfigToPreferences(AppConfig& config,
                                Preferences& prefs,
                                bool emit_logs);

namespace
{
// ESP NVS key names must stay within 15 characters.
constexpr const char* kChatKeyMeshOverrideDuty = "mesh_ovr_dc";
constexpr const char* kChatKeyMeshFreqOverride = "mesh_freq_ovr";
constexpr const char* kChatKeyMeshIgnoreMqtt = "mesh_ign_mqtt";
constexpr const char* kChatKeyMcRegionPreset = "mc_reg_preset";
constexpr const char* kChatKeySecondaryEnabled = "sec_enabled";
constexpr const char* kChatKeyPrimaryDownlink = "pri_downlink";
constexpr const char* kChatKeySecondaryUplink = "sec_uplink";
constexpr const char* kChatKeySecondaryDownlink = "sec_downlink";
constexpr const char* kGpsKeyMotionSensorId = "motion_sensor";
constexpr const char* kSettingsKeyMapTrackInterval = "map_track_int";
constexpr const char* kSettingsKeyMapTrackFormat = "map_track_fmt";
constexpr const char* kSettingsKeyPrivacyNmeaSentence = "priv_nmea_sent";

const char* safe_label(const char* value)
{
    return value ? value : "<null>";
}

const char* bool_label(bool value)
{
    return value ? "true" : "false";
}

bool begin_namespace(Preferences& prefs,
                     const char* ns,
                     bool read_only,
                     const char* phase,
                     bool emit_logs)
{
    const bool ok = ns != nullptr && prefs.begin(ns, read_only);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][%s] ns=%s mode=%s open=%s\n",
                      safe_label(phase),
                      safe_label(ns),
                      read_only ? "ro" : "rw",
                      bool_label(ok));
    }
    return ok;
}

bool get_bool_logged(Preferences& prefs,
                     const char* ns,
                     const char* key,
                     bool default_value,
                     bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const bool value = prefs.getBool(key, default_value);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=bool source=%s value=%s default=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      bool_label(value),
                      bool_label(default_value));
    }
    return value;
}

uint8_t get_uchar_logged(Preferences& prefs,
                         const char* ns,
                         const char* key,
                         uint8_t default_value,
                         bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const uint8_t value = prefs.getUChar(key, default_value);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=uchar source=%s value=%u default=%u\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      static_cast<unsigned>(value),
                      static_cast<unsigned>(default_value));
    }
    return value;
}

int8_t get_char_logged(Preferences& prefs,
                       const char* ns,
                       const char* key,
                       int8_t default_value,
                       bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const int8_t value = prefs.getChar(key, default_value);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=char source=%s value=%d default=%d\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      static_cast<int>(value),
                      static_cast<int>(default_value));
    }
    return value;
}

uint16_t get_ushort_logged(Preferences& prefs,
                           const char* ns,
                           const char* key,
                           uint16_t default_value,
                           bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const uint16_t value = prefs.getUShort(key, default_value);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=ushort source=%s value=%u default=%u\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      static_cast<unsigned>(value),
                      static_cast<unsigned>(default_value));
    }
    return value;
}

uint32_t get_uint_logged(Preferences& prefs,
                         const char* ns,
                         const char* key,
                         uint32_t default_value,
                         bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const uint32_t value = prefs.getUInt(key, default_value);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=uint source=%s value=%lu default=%lu\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      static_cast<unsigned long>(value),
                      static_cast<unsigned long>(default_value));
    }
    return value;
}

int get_int_logged(Preferences& prefs,
                   const char* ns,
                   const char* key,
                   int default_value,
                   bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const int value = prefs.getInt(key, default_value);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=int source=%s value=%d default=%d\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      value,
                      default_value);
    }
    return value;
}

float get_float_logged(Preferences& prefs,
                       const char* ns,
                       const char* key,
                       float default_value,
                       bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const float value = prefs.getFloat(key, default_value);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=float source=%s value=%.3f default=%.3f\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      static_cast<double>(value),
                      static_cast<double>(default_value));
    }
    return value;
}

String get_string_logged(Preferences& prefs,
                         const char* ns,
                         const char* key,
                         const char* default_value,
                         bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const String value = prefs.getString(key, default_value ? default_value : "");
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=string source=%s len=%lu value=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      static_cast<unsigned long>(value.length()),
                      safe_label(value.c_str()));
    }
    return value;
}

size_t get_bytes_logged(Preferences& prefs,
                        const char* ns,
                        const char* key,
                        void* buffer,
                        size_t len,
                        bool emit_logs)
{
    const bool exists = prefs.isKey(key);
    const size_t read = prefs.getBytes(key, buffer, len);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][READ] ns=%s key=%s type=bytes source=%s len=%lu\n",
                      safe_label(ns),
                      safe_label(key),
                      exists ? "stored" : "default",
                      static_cast<unsigned long>(read));
    }
    return read;
}

bool put_bool_logged(Preferences& prefs,
                     const char* ns,
                     const char* key,
                     bool value,
                     bool emit_logs)
{
    const bool ok = prefs.putBool(key, value) == sizeof(uint8_t);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][WRITE] ns=%s key=%s type=bool value=%s ok=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      bool_label(value),
                      bool_label(ok));
    }
    return ok;
}

bool put_uchar_logged(Preferences& prefs,
                      const char* ns,
                      const char* key,
                      uint8_t value,
                      bool emit_logs)
{
    const bool ok = prefs.putUChar(key, value) == sizeof(uint8_t);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][WRITE] ns=%s key=%s type=uchar value=%u ok=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      static_cast<unsigned>(value),
                      bool_label(ok));
    }
    return ok;
}

bool put_char_logged(Preferences& prefs,
                     const char* ns,
                     const char* key,
                     int8_t value,
                     bool emit_logs)
{
    const bool ok = prefs.putChar(key, value) == sizeof(int8_t);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][WRITE] ns=%s key=%s type=char value=%d ok=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      static_cast<int>(value),
                      bool_label(ok));
    }
    return ok;
}

bool put_ushort_logged(Preferences& prefs,
                       const char* ns,
                       const char* key,
                       uint16_t value,
                       bool emit_logs)
{
    const bool ok = prefs.putUShort(key, value) == sizeof(uint16_t);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][WRITE] ns=%s key=%s type=ushort value=%u ok=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      static_cast<unsigned>(value),
                      bool_label(ok));
    }
    return ok;
}

bool put_uint_logged(Preferences& prefs,
                     const char* ns,
                     const char* key,
                     uint32_t value,
                     bool emit_logs)
{
    const bool ok = prefs.putUInt(key, value) == sizeof(uint32_t);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][WRITE] ns=%s key=%s type=uint value=%lu ok=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      static_cast<unsigned long>(value),
                      bool_label(ok));
    }
    return ok;
}

bool put_float_logged(Preferences& prefs,
                      const char* ns,
                      const char* key,
                      float value,
                      bool emit_logs)
{
    const bool ok = prefs.putFloat(key, value) == sizeof(float);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][WRITE] ns=%s key=%s type=float value=%.3f ok=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      static_cast<double>(value),
                      bool_label(ok));
    }
    return ok;
}

bool put_string_logged(Preferences& prefs,
                       const char* ns,
                       const char* key,
                       const char* value,
                       bool emit_logs)
{
    const char* safe_value = value ? value : "";
    const size_t expected = strlen(safe_value);
    const bool ok = prefs.putString(key, safe_value) == expected;
    if (emit_logs)
    {
        Serial.printf("[AppCfg][WRITE] ns=%s key=%s type=string len=%lu ok=%s value=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      static_cast<unsigned long>(expected),
                      bool_label(ok),
                      safe_value);
    }
    return ok;
}

bool put_bytes_logged(Preferences& prefs,
                      const char* ns,
                      const char* key,
                      const void* value,
                      size_t len,
                      bool emit_logs)
{
    const bool ok = prefs.putBytes(key, value, len) == len;
    if (emit_logs)
    {
        Serial.printf("[AppCfg][WRITE] ns=%s key=%s type=bytes len=%lu ok=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      static_cast<unsigned long>(len),
                      bool_label(ok));
    }
    return ok;
}

bool remove_key_logged(Preferences& prefs,
                       const char* ns,
                       const char* key,
                       bool emit_logs)
{
    const bool ok = prefs.remove(key);
    if (emit_logs)
    {
        Serial.printf("[AppCfg][REMOVE] ns=%s key=%s ok=%s\n",
                      safe_label(ns),
                      safe_label(key),
                      bool_label(ok));
    }
    return ok;
}

void log_change_bool(const char* field, bool before, bool after, bool& any_change)
{
    if (before == after)
    {
        return;
    }
    any_change = true;
    Serial.printf("[AppCfg][DELTA] %s %s -> %s\n",
                  safe_label(field),
                  bool_label(before),
                  bool_label(after));
}

void log_change_u8(const char* field, uint8_t before, uint8_t after, bool& any_change)
{
    if (before == after)
    {
        return;
    }
    any_change = true;
    Serial.printf("[AppCfg][DELTA] %s %u -> %u\n",
                  safe_label(field),
                  static_cast<unsigned>(before),
                  static_cast<unsigned>(after));
}

void log_change_u16(const char* field, uint16_t before, uint16_t after, bool& any_change)
{
    if (before == after)
    {
        return;
    }
    any_change = true;
    Serial.printf("[AppCfg][DELTA] %s %u -> %u\n",
                  safe_label(field),
                  static_cast<unsigned>(before),
                  static_cast<unsigned>(after));
}

void log_change_u32(const char* field, uint32_t before, uint32_t after, bool& any_change)
{
    if (before == after)
    {
        return;
    }
    any_change = true;
    Serial.printf("[AppCfg][DELTA] %s %lu -> %lu\n",
                  safe_label(field),
                  static_cast<unsigned long>(before),
                  static_cast<unsigned long>(after));
}

void log_change_i8(const char* field, int8_t before, int8_t after, bool& any_change)
{
    if (before == after)
    {
        return;
    }
    any_change = true;
    Serial.printf("[AppCfg][DELTA] %s %d -> %d\n",
                  safe_label(field),
                  static_cast<int>(before),
                  static_cast<int>(after));
}

void log_change_float(const char* field, float before, float after, bool& any_change)
{
    if (before == after)
    {
        return;
    }
    any_change = true;
    Serial.printf("[AppCfg][DELTA] %s %.3f -> %.3f\n",
                  safe_label(field),
                  static_cast<double>(before),
                  static_cast<double>(after));
}

void log_change_text(const char* field,
                     const char* before,
                     const char* after,
                     bool& any_change)
{
    const char* safe_before = before ? before : "";
    const char* safe_after = after ? after : "";
    if (strcmp(safe_before, safe_after) == 0)
    {
        return;
    }
    any_change = true;
    Serial.printf("[AppCfg][DELTA] %s \"%s\" -> \"%s\"\n",
                  safe_label(field),
                  safe_before,
                  safe_after);
}

void log_change_protocol(const char* field,
                         chat::MeshProtocol before,
                         chat::MeshProtocol after,
                         bool& any_change)
{
    if (before == after)
    {
        return;
    }
    any_change = true;
    Serial.printf("[AppCfg][DELTA] %s %s -> %s\n",
                  safe_label(field),
                  chat::infra::meshProtocolName(before),
                  chat::infra::meshProtocolName(after));
}

void log_config_delta(const AppConfig& before, const AppConfig& after)
{
    bool any_change = false;

    log_change_protocol("mesh_protocol", before.mesh_protocol, after.mesh_protocol, any_change);
    log_change_text("node_name", before.node_name, after.node_name, any_change);
    log_change_text("short_name", before.short_name, after.short_name, any_change);
    log_change_bool("ble_enabled", before.ble_enabled, after.ble_enabled, any_change);

    log_change_bool("chat_policy.relay", before.chat_policy.enable_relay, after.chat_policy.enable_relay, any_change);
    log_change_u8("chat_policy.hop_limit", before.chat_policy.hop_limit_default,
                  after.chat_policy.hop_limit_default, any_change);
    log_change_bool("chat_policy.ack_bcast", before.chat_policy.ack_for_broadcast,
                    after.chat_policy.ack_for_broadcast, any_change);
    log_change_bool("chat_policy.ack_squad", before.chat_policy.ack_for_squad,
                    after.chat_policy.ack_for_squad, any_change);
    log_change_u8("chat_policy.max_retries", before.chat_policy.max_tx_retries,
                  after.chat_policy.max_tx_retries, any_change);

    log_change_u8("meshtastic.region", before.meshtastic_config.region, after.meshtastic_config.region, any_change);
    log_change_u8("meshtastic.modem_preset", before.meshtastic_config.modem_preset,
                  after.meshtastic_config.modem_preset, any_change);
    log_change_i8("meshtastic.tx_power", before.meshtastic_config.tx_power,
                  after.meshtastic_config.tx_power, any_change);
    log_change_bool("meshtastic.tx_enabled", before.meshtastic_config.tx_enabled,
                    after.meshtastic_config.tx_enabled, any_change);
    log_change_u16("meshtastic.channel_num", before.meshtastic_config.channel_num,
                   after.meshtastic_config.channel_num, any_change);
    log_change_float("meshtastic.freq_offset", before.meshtastic_config.frequency_offset_mhz,
                     after.meshtastic_config.frequency_offset_mhz, any_change);
    log_change_float("meshtastic.freq_override", before.meshtastic_config.override_frequency_mhz,
                     after.meshtastic_config.override_frequency_mhz, any_change);

    log_change_u8("meshcore.region_preset", before.meshcore_config.meshcore_region_preset,
                  after.meshcore_config.meshcore_region_preset, any_change);
    log_change_float("meshcore.freq_mhz", before.meshcore_config.meshcore_freq_mhz,
                     after.meshcore_config.meshcore_freq_mhz, any_change);
    log_change_float("meshcore.bw_khz", before.meshcore_config.meshcore_bw_khz,
                     after.meshcore_config.meshcore_bw_khz, any_change);
    log_change_u8("meshcore.sf", before.meshcore_config.meshcore_sf,
                  after.meshcore_config.meshcore_sf, any_change);
    log_change_u8("meshcore.cr", before.meshcore_config.meshcore_cr,
                  after.meshcore_config.meshcore_cr, any_change);
    log_change_i8("meshcore.tx_power", before.meshcore_config.tx_power,
                  after.meshcore_config.tx_power, any_change);
    log_change_text("meshcore.channel_name", before.meshcore_config.meshcore_channel_name,
                    after.meshcore_config.meshcore_channel_name, any_change);

    log_change_float("rnode.freq_mhz", before.rnode_config.override_frequency_mhz,
                     after.rnode_config.override_frequency_mhz, any_change);
    log_change_float("rnode.bw_khz", before.rnode_config.bandwidth_khz,
                     after.rnode_config.bandwidth_khz, any_change);
    log_change_u8("rnode.sf", before.rnode_config.spread_factor,
                  after.rnode_config.spread_factor, any_change);
    log_change_u8("rnode.cr", before.rnode_config.coding_rate,
                  after.rnode_config.coding_rate, any_change);
    log_change_i8("rnode.tx_power", before.rnode_config.tx_power,
                  after.rnode_config.tx_power, any_change);
    log_change_bool("rnode.tx_enabled", before.rnode_config.tx_enabled,
                    after.rnode_config.tx_enabled, any_change);

    log_change_bool("primary_enabled", before.primary_enabled, after.primary_enabled, any_change);
    log_change_bool("secondary_enabled", before.secondary_enabled, after.secondary_enabled, any_change);
    log_change_bool("primary_uplink", before.primary_uplink_enabled, after.primary_uplink_enabled, any_change);
    log_change_bool("primary_downlink", before.primary_downlink_enabled, after.primary_downlink_enabled, any_change);
    log_change_bool("secondary_uplink", before.secondary_uplink_enabled, after.secondary_uplink_enabled, any_change);
    log_change_bool("secondary_downlink", before.secondary_downlink_enabled, after.secondary_downlink_enabled, any_change);

    log_change_u32("gps_interval_ms", before.gps_interval_ms, after.gps_interval_ms, any_change);
    log_change_u8("gps_mode", before.gps_mode, after.gps_mode, any_change);
    log_change_u8("gps_sat_mask", before.gps_sat_mask, after.gps_sat_mask, any_change);
    log_change_u8("gps_strategy", before.gps_strategy, after.gps_strategy, any_change);
    log_change_u8("gps_alt_ref", before.gps_alt_ref, after.gps_alt_ref, any_change);
    log_change_u8("gps_coord_format", before.gps_coord_format, after.gps_coord_format, any_change);
    log_change_u32("motion_idle_ms", before.motion_config.idle_timeout_ms,
                   after.motion_config.idle_timeout_ms, any_change);
    log_change_u8("motion_sensor_id", before.motion_config.sensor_id,
                  after.motion_config.sensor_id, any_change);

    log_change_u8("map_coord_system", before.map_coord_system, after.map_coord_system, any_change);
    log_change_u8("map_source", before.map_source, after.map_source, any_change);
    log_change_bool("map_contour_enabled", before.map_contour_enabled, after.map_contour_enabled, any_change);
    log_change_bool("map_track_enabled", before.map_track_enabled, after.map_track_enabled, any_change);
    log_change_u8("map_track_interval", before.map_track_interval, after.map_track_interval, any_change);
    log_change_u8("map_track_format", before.map_track_format, after.map_track_format, any_change);

    log_change_u8("chat_channel", before.chat_channel, after.chat_channel, any_change);
    log_change_bool("net_duty_cycle", before.net_duty_cycle, after.net_duty_cycle, any_change);
    log_change_u8("net_util", before.net_channel_util, after.net_channel_util, any_change);

    log_change_u8("privacy_encrypt_mode", before.privacy_encrypt_mode, after.privacy_encrypt_mode, any_change);
    log_change_u8("privacy_nmea_output", before.privacy_nmea_output, after.privacy_nmea_output, any_change);
    log_change_u8("privacy_nmea_sentence", before.privacy_nmea_sentence, after.privacy_nmea_sentence, any_change);

    log_change_bool("route_enabled", before.route_enabled, after.route_enabled, any_change);
    log_change_text("route_path", before.route_path, after.route_path, any_change);

    log_change_bool("aprs.enabled", before.aprs.enabled, after.aprs.enabled, any_change);
    log_change_text("aprs.igate_callsign", before.aprs.igate_callsign, after.aprs.igate_callsign, any_change);
    log_change_u8("aprs.igate_ssid", before.aprs.igate_ssid, after.aprs.igate_ssid, any_change);
    log_change_text("aprs.tocall", before.aprs.tocall, after.aprs.tocall, any_change);
    log_change_text("aprs.path", before.aprs.path, after.aprs.path, any_change);
    log_change_u16("aprs.tx_min_interval_s", before.aprs.tx_min_interval_s, after.aprs.tx_min_interval_s, any_change);
    log_change_u16("aprs.dedupe_window_s", before.aprs.dedupe_window_s, after.aprs.dedupe_window_s, any_change);
    log_change_u16("aprs.position_interval_s", before.aprs.position_interval_s,
                   after.aprs.position_interval_s, any_change);
    log_change_bool("aprs.self_enable", before.aprs.self_enable, after.aprs.self_enable, any_change);
    log_change_text("aprs.self_callsign", before.aprs.self_callsign, after.aprs.self_callsign, any_change);

    if (!any_change)
    {
        Serial.println("[AppCfg][DELTA] no field changes");
    }
}

void log_config_summary(const char* phase, const AppConfig& config)
{
    Serial.printf("[AppCfg][%s][identity] protocol=%s node=%s short=%s ble=%s\n",
                  safe_label(phase),
                  chat::infra::meshProtocolName(config.mesh_protocol),
                  config.node_name,
                  config.short_name,
                  bool_label(config.ble_enabled));
    Serial.printf("[AppCfg][%s][radio] region=%u mc_preset=%u relay=%s hop_limit=%u chat_channel=%u tx_power_mt=%d tx_power_mc=%d tx_power_rn=%d\n",
                  safe_label(phase),
                  static_cast<unsigned>(config.meshtastic_config.region),
                  static_cast<unsigned>(config.meshcore_config.meshcore_region_preset),
                  bool_label(config.chat_policy.enable_relay),
                  static_cast<unsigned>(config.chat_policy.hop_limit_default),
                  static_cast<unsigned>(config.chat_channel),
                  static_cast<int>(config.meshtastic_config.tx_power),
                  static_cast<int>(config.meshcore_config.tx_power),
                  static_cast<int>(config.rnode_config.tx_power));
    Serial.printf("[AppCfg][%s][gps] interval_ms=%lu mode=%u sat_mask=%u strategy=%u alt_ref=%u coord_fmt=%u motion_idle_ms=%lu sensor_id=%u\n",
                  safe_label(phase),
                  static_cast<unsigned long>(config.gps_interval_ms),
                  static_cast<unsigned>(config.gps_mode),
                  static_cast<unsigned>(config.gps_sat_mask),
                  static_cast<unsigned>(config.gps_strategy),
                  static_cast<unsigned>(config.gps_alt_ref),
                  static_cast<unsigned>(config.gps_coord_format),
                  static_cast<unsigned long>(config.motion_config.idle_timeout_ms),
                  static_cast<unsigned>(config.motion_config.sensor_id));
    Serial.printf("[AppCfg][%s][map] coord=%u source=%u contour=%s track=%s track_interval=%u track_format=%u route=%s route_path=%s\n",
                  safe_label(phase),
                  static_cast<unsigned>(config.map_coord_system),
                  static_cast<unsigned>(config.map_source),
                  bool_label(config.map_contour_enabled),
                  bool_label(config.map_track_enabled),
                  static_cast<unsigned>(config.map_track_interval),
                  static_cast<unsigned>(config.map_track_format),
                  bool_label(config.route_enabled),
                  config.route_path);
    Serial.printf("[AppCfg][%s][privacy] duty_cycle=%s net_util=%u encrypt=%u nmea=%u sentence=%u\n",
                  safe_label(phase),
                  bool_label(config.net_duty_cycle),
                  static_cast<unsigned>(config.net_channel_util),
                  static_cast<unsigned>(config.privacy_encrypt_mode),
                  static_cast<unsigned>(config.privacy_nmea_output),
                  static_cast<unsigned>(config.privacy_nmea_sentence));
    Serial.printf("[AppCfg][%s][aprs] enabled=%s igate=%s-%u tocall=%s path=%s pos_interval=%u self=%s self_call=%s node_map_len=%u\n",
                  safe_label(phase),
                  bool_label(config.aprs.enabled),
                  config.aprs.igate_callsign,
                  static_cast<unsigned>(config.aprs.igate_ssid),
                  config.aprs.tocall,
                  config.aprs.path,
                  static_cast<unsigned>(config.aprs.position_interval_s),
                  bool_label(config.aprs.self_enable),
                  config.aprs.self_callsign,
                  static_cast<unsigned>(config.aprs.node_map_len));
}

} // namespace

bool loadAppConfigFromPreferences(AppConfig& config,
                                  Preferences& prefs,
                                  bool emit_logs)
{
    auto& chat_policy = config.chat_policy;
    auto& meshtastic_config = config.meshtastic_config;
    auto& meshcore_config = config.meshcore_config;
    auto& rnode_config = config.rnode_config;
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
    auto& ble_enabled = config.ble_enabled;
    auto& chat_channel = config.chat_channel;
    auto& net_duty_cycle = config.net_duty_cycle;
    auto& net_channel_util = config.net_channel_util;
    auto& privacy_encrypt_mode = config.privacy_encrypt_mode;
    auto& privacy_nmea_output = config.privacy_nmea_output;
    auto& privacy_nmea_sentence = config.privacy_nmea_sentence;
    auto& route_enabled = config.route_enabled;
    auto& route_path = config.route_path;
    auto& aprs = config.aprs;

    if (emit_logs)
    {
        Serial.println("[AppCfg][LOAD] begin");
    }

    if (begin_namespace(prefs, "chat", true, "LOAD", emit_logs))
    {
        auto get_bool = [&](const char* key, bool default_value) -> bool
        {
            return get_bool_logged(prefs, "chat", key, default_value, emit_logs);
        };
        auto get_uchar = [&](const char* key, uint8_t default_value) -> uint8_t
        {
            return get_uchar_logged(prefs, "chat", key, default_value, emit_logs);
        };
        auto get_char = [&](const char* key, int8_t default_value) -> int8_t
        {
            return get_char_logged(prefs, "chat", key, default_value, emit_logs);
        };
        auto get_ushort = [&](const char* key, uint16_t default_value) -> uint16_t
        {
            return get_ushort_logged(prefs, "chat", key, default_value, emit_logs);
        };
        auto get_int = [&](const char* key, int default_value) -> int
        {
            return get_int_logged(prefs, "chat", key, default_value, emit_logs);
        };
        auto get_float = [&](const char* key, float default_value) -> float
        {
            return get_float_logged(prefs, "chat", key, default_value, emit_logs);
        };
        auto get_string = [&](const char* key, const char* default_value) -> String
        {
            return get_string_logged(prefs, "chat", key, default_value, emit_logs);
        };
        auto get_bytes = [&](const char* key, void* buffer, size_t len) -> size_t
        {
            return get_bytes_logged(prefs, "chat", key, buffer, len, emit_logs);
        };

        // Load policy
        chat_policy.enable_relay = get_bool("relay", true);
        chat_policy.hop_limit_default = get_uchar("hop_limit", 2);
        chat_policy.ack_for_broadcast = get_bool("ack_bcast", false);
        chat_policy.ack_for_squad = get_bool("ack_squad", true);
        chat_policy.max_tx_retries = get_uchar("max_retries", 1);

        // Load meshtastic config (legacy-compatible keys)
        meshtastic_config.region = get_uchar("region", 0);
        if (meshtastic_config.region == 0)
        {
            meshtastic_config.region = AppConfig::kDefaultRegionCode;
        }
        meshtastic_config.modem_preset = get_uchar("modem_preset", 0);
        meshtastic_config.tx_power = get_char("tx_power", 14);
        meshtastic_config.hop_limit = get_uchar("mesh_hop_limit", 2);
        meshtastic_config.enable_relay = get_bool("mesh_relay", true);
        meshtastic_config.use_preset = get_bool("use_preset", true);
        meshtastic_config.bandwidth_khz = get_float("mesh_bw_khz", meshtastic_config.bandwidth_khz);
        meshtastic_config.spread_factor = get_uchar("mesh_sf", meshtastic_config.spread_factor);
        meshtastic_config.coding_rate = get_uchar("mesh_cr", meshtastic_config.coding_rate);
        meshtastic_config.tx_enabled = get_bool("mesh_tx_en", true);
        meshtastic_config.override_duty_cycle = get_bool(kChatKeyMeshOverrideDuty, false);
        meshtastic_config.channel_num = get_ushort("mesh_ch_num", 0);
        meshtastic_config.frequency_offset_mhz = get_float("mesh_freq_off", 0.0f);
        meshtastic_config.override_frequency_mhz = get_float(kChatKeyMeshFreqOverride, 0.0f);
        meshtastic_config.ignore_mqtt = get_bool(kChatKeyMeshIgnoreMqtt, false);
        meshtastic_config.config_ok_to_mqtt = get_bool("mesh_ok_to_mqtt", false);

        // Load meshcore profile (protocol-specific keys)
        meshcore_config.meshcore_freq_mhz = get_float("mc_freq", meshcore_config.meshcore_freq_mhz);
        meshcore_config.meshcore_bw_khz = get_float("mc_bw", meshcore_config.meshcore_bw_khz);
        meshcore_config.meshcore_sf = get_uchar("mc_sf", meshcore_config.meshcore_sf);
        meshcore_config.meshcore_cr = get_uchar("mc_cr", meshcore_config.meshcore_cr);
        meshcore_config.meshcore_region_preset = get_uchar(kChatKeyMcRegionPreset,
                                                           meshcore_config.meshcore_region_preset);
        meshcore_config.tx_power = get_char("mc_tx", meshcore_config.tx_power);
        meshcore_config.meshcore_client_repeat = get_bool("mc_repeat", false);
        meshcore_config.meshcore_rx_delay_base = get_float("mc_rx_delay", meshcore_config.meshcore_rx_delay_base);
        meshcore_config.meshcore_airtime_factor = get_float("mc_airtime", meshcore_config.meshcore_airtime_factor);
        meshcore_config.meshcore_flood_max = get_uchar("mc_flood_max", meshcore_config.meshcore_flood_max);
        meshcore_config.meshcore_multi_acks = get_bool("mc_multi_acks", false);
        meshcore_config.meshcore_channel_slot = get_uchar("mc_ch_slot", 0);
        meshcore_config.tx_enabled = get_bool("mc_tx_en", meshcore_config.tx_enabled);
        String mc_name = get_string("mc_ch_name", meshcore_config.meshcore_channel_name);
        strncpy(meshcore_config.meshcore_channel_name, mc_name.c_str(),
                sizeof(meshcore_config.meshcore_channel_name) - 1);
        meshcore_config.meshcore_channel_name[sizeof(meshcore_config.meshcore_channel_name) - 1] = '\0';
        get_bytes("mc_ch_key", meshcore_config.secondary_key, sizeof(meshcore_config.secondary_key));

        rnode_config.override_frequency_mhz = get_float("rn_freq", rnode_config.override_frequency_mhz);
        rnode_config.bandwidth_khz = get_float("rn_bw", rnode_config.bandwidth_khz);
        rnode_config.spread_factor = get_uchar("rn_sf", rnode_config.spread_factor);
        rnode_config.coding_rate = get_uchar("rn_cr", rnode_config.coding_rate);
        rnode_config.tx_power = get_char("rn_tx", rnode_config.tx_power);
        rnode_config.tx_enabled = get_bool("rn_tx_en", rnode_config.tx_enabled);

        const uint8_t mesh_protocol_raw = get_uchar("mesh_protocol", 0xFF);
        if (chat::infra::isValidMeshProtocolValue(mesh_protocol_raw))
        {
            mesh_protocol = chat::infra::meshProtocolFromRaw(mesh_protocol_raw);
        }
        else
        {
            const int legacy_protocol = get_int("mesh_protocol",
                                                static_cast<int>(chat::MeshProtocol::Meshtastic));
            mesh_protocol = chat::infra::meshProtocolFromRaw(
                static_cast<uint8_t>(legacy_protocol),
                chat::MeshProtocol::Meshtastic);
        }

        // Load device name
        size_t len = get_bytes("node_name", node_name, sizeof(node_name) - 1);
        if (len > 0)
        {
            node_name[len] = '\0';
        }
        len = get_bytes("short_name", short_name, sizeof(short_name) - 1);
        if (len > 0)
        {
            short_name[len] = '\0';
        }

        // Load channel settings
        primary_enabled = get_bool("primary_enabled", true);
        secondary_enabled = get_bool(kChatKeySecondaryEnabled, false);
        primary_uplink_enabled = get_bool("primary_uplink", false);
        primary_downlink_enabled = get_bool(kChatKeyPrimaryDownlink, false);
        secondary_uplink_enabled = get_bool(kChatKeySecondaryUplink, false);
        secondary_downlink_enabled = get_bool(kChatKeySecondaryDownlink, false);
        get_bytes("secondary_key", secondary_key, sizeof(secondary_key));
        memcpy(meshtastic_config.secondary_key, secondary_key, sizeof(meshtastic_config.secondary_key));

        prefs.end();
    }

    auto clamp_tx_power = [](int8_t value) -> int8_t
    {
        if (value < AppConfig::kTxPowerMinDbm) return AppConfig::kTxPowerMinDbm;
        if (value > AppConfig::kTxPowerMaxDbm) return AppConfig::kTxPowerMaxDbm;
        return value;
    };
    meshtastic_config.tx_power = clamp_tx_power(meshtastic_config.tx_power);
    meshcore_config.tx_power = clamp_tx_power(meshcore_config.tx_power);
    rnode_config.tx_power = clamp_tx_power(rnode_config.tx_power);

    if (begin_namespace(prefs, "gps", true, "LOAD", emit_logs))
    {
        auto get_uchar = [&](const char* key, uint8_t default_value) -> uint8_t
        {
            return get_uchar_logged(prefs, "gps", key, default_value, emit_logs);
        };
        auto get_uint = [&](const char* key, uint32_t default_value) -> uint32_t
        {
            return get_uint_logged(prefs, "gps", key, default_value, emit_logs);
        };

        gps_interval_ms = get_uint("gps_interval", gps_interval_ms);
        gps_mode = get_uchar("gps_mode", gps_mode);
        gps_sat_mask = get_uchar("gps_sat_mask", gps_sat_mask);
        gps_strategy = get_uchar("gps_strategy", gps_strategy);
        gps_alt_ref = get_uchar("gps_alt_ref", gps_alt_ref);
        gps_coord_format = get_uchar("gps_coord_fmt", gps_coord_format);
        motion_config.idle_timeout_ms = get_uint("motion_idle_ms", motion_config.idle_timeout_ms);
        motion_config.sensor_id = get_uchar(kGpsKeyMotionSensorId, motion_config.sensor_id);
        prefs.end();
    }

    if (begin_namespace(prefs, "settings", true, "LOAD", emit_logs))
    {
        auto get_bool = [&](const char* key, bool default_value) -> bool
        {
            return get_bool_logged(prefs, "settings", key, default_value, emit_logs);
        };
        auto get_uchar = [&](const char* key, uint8_t default_value) -> uint8_t
        {
            return get_uchar_logged(prefs, "settings", key, default_value, emit_logs);
        };
        auto get_string = [&](const char* key, const char* default_value) -> String
        {
            return get_string_logged(prefs, "settings", key, default_value, emit_logs);
        };

        map_coord_system = get_uchar("map_coord", map_coord_system);
        map_source = get_uchar("map_source", map_source);
        if (map_source > 2)
        {
            map_source = 0;
        }
        map_contour_enabled = get_bool("map_contour", map_contour_enabled);
        map_track_enabled = get_bool("map_track", map_track_enabled);
        map_track_interval = get_uchar(kSettingsKeyMapTrackInterval, map_track_interval);
        map_track_format = get_uchar(kSettingsKeyMapTrackFormat, map_track_format);
        ble_enabled = get_bool("ble_enabled", ble_enabled);
        chat_channel = get_uchar("chat_channel", chat_channel);
        net_duty_cycle = get_bool("net_duty_cycle", net_duty_cycle);
        net_channel_util = get_uchar("net_util", net_channel_util);
        privacy_encrypt_mode = get_uchar("privacy_encrypt", privacy_encrypt_mode);
        privacy_nmea_output = get_uchar("privacy_nmea", privacy_nmea_output);
        privacy_nmea_sentence = get_uchar(kSettingsKeyPrivacyNmeaSentence, privacy_nmea_sentence);
        route_enabled = get_bool("route_enabled", route_enabled);
        String path = get_string("route_path", route_path);
        strncpy(route_path, path.c_str(), sizeof(route_path) - 1);
        route_path[sizeof(route_path) - 1] = '\0';
        prefs.end();
    }

    if (begin_namespace(prefs, "aprs", true, "LOAD", emit_logs))
    {
        auto get_bool = [&](const char* key, bool default_value) -> bool
        {
            return get_bool_logged(prefs, "aprs", key, default_value, emit_logs);
        };
        auto get_uchar = [&](const char* key, uint8_t default_value) -> uint8_t
        {
            return get_uchar_logged(prefs, "aprs", key, default_value, emit_logs);
        };
        auto get_ushort = [&](const char* key, uint16_t default_value) -> uint16_t
        {
            return get_ushort_logged(prefs, "aprs", key, default_value, emit_logs);
        };
        auto get_char = [&](const char* key, int8_t default_value) -> int8_t
        {
            return get_char_logged(prefs, "aprs", key, default_value, emit_logs);
        };
        auto get_string = [&](const char* key, const char* default_value) -> String
        {
            return get_string_logged(prefs, "aprs", key, default_value, emit_logs);
        };
        auto get_bytes = [&](const char* key, void* buffer, size_t len) -> size_t
        {
            return get_bytes_logged(prefs, "aprs", key, buffer, len, emit_logs);
        };

        aprs.enabled = get_bool("enabled", aprs.enabled);
        String call = get_string("igate_call", aprs.igate_callsign);
        strncpy(aprs.igate_callsign, call.c_str(), sizeof(aprs.igate_callsign) - 1);
        aprs.igate_callsign[sizeof(aprs.igate_callsign) - 1] = '\0';
        aprs.igate_ssid = get_uchar("igate_ssid", aprs.igate_ssid);
        String tocall = get_string("to_call", aprs.tocall);
        strncpy(aprs.tocall, tocall.c_str(), sizeof(aprs.tocall) - 1);
        aprs.tocall[sizeof(aprs.tocall) - 1] = '\0';
        String aprs_path = get_string("path", aprs.path);
        strncpy(aprs.path, aprs_path.c_str(), sizeof(aprs.path) - 1);
        aprs.path[sizeof(aprs.path) - 1] = '\0';
        aprs.tx_min_interval_s = get_ushort("tx_min", aprs.tx_min_interval_s);
        aprs.dedupe_window_s = get_ushort("dedupe", aprs.dedupe_window_s);
        aprs.symbol_table = static_cast<char>(get_char("sym_tab", aprs.symbol_table));
        aprs.symbol_code = static_cast<char>(get_char("sym_code", aprs.symbol_code));
        aprs.position_interval_s = get_ushort("pos_interval", aprs.position_interval_s);
        size_t map_len = get_bytes("node_map", aprs.node_map, sizeof(aprs.node_map));
        if (map_len > sizeof(aprs.node_map))
        {
            map_len = sizeof(aprs.node_map);
        }
        aprs.node_map_len = static_cast<uint8_t>(map_len);
        aprs.self_enable = get_bool("self_enable", aprs.self_enable);
        String self_call = get_string("self_call", aprs.self_callsign);
        strncpy(aprs.self_callsign, self_call.c_str(), sizeof(aprs.self_callsign) - 1);
        aprs.self_callsign[sizeof(aprs.self_callsign) - 1] = '\0';
        prefs.end();
    }

    if (emit_logs)
    {
        log_config_summary("LOAD", config);
        Serial.println("[AppCfg][LOAD] done");
    }
    return true;
}

bool saveAppConfigToPreferences(AppConfig& config,
                                Preferences& prefs,
                                bool emit_logs)
{
    auto& chat_policy = config.chat_policy;
    auto& meshtastic_config = config.meshtastic_config;
    auto& meshcore_config = config.meshcore_config;
    auto& rnode_config = config.rnode_config;
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
    auto& ble_enabled = config.ble_enabled;
    auto& chat_channel = config.chat_channel;
    auto& net_duty_cycle = config.net_duty_cycle;
    auto& net_channel_util = config.net_channel_util;
    auto& privacy_encrypt_mode = config.privacy_encrypt_mode;
    auto& privacy_nmea_output = config.privacy_nmea_output;
    auto& privacy_nmea_sentence = config.privacy_nmea_sentence;
    auto& route_enabled = config.route_enabled;
    auto& route_path = config.route_path;
    auto& aprs = config.aprs;

    if (emit_logs)
    {
        Serial.println("[AppCfg][SAVE] begin");
        log_config_summary("SAVE", config);
    }

    bool ok = true;

    if (!begin_namespace(prefs, "chat", false, "SAVE", emit_logs))
    {
        return false;
    }
    {
        auto put_bool = [&](const char* key, bool value)
        {
            ok = put_bool_logged(prefs, "chat", key, value, emit_logs) && ok;
        };
        auto put_uchar = [&](const char* key, uint8_t value)
        {
            ok = put_uchar_logged(prefs, "chat", key, value, emit_logs) && ok;
        };
        auto put_char = [&](const char* key, int8_t value)
        {
            ok = put_char_logged(prefs, "chat", key, value, emit_logs) && ok;
        };
        auto put_ushort = [&](const char* key, uint16_t value)
        {
            ok = put_ushort_logged(prefs, "chat", key, value, emit_logs) && ok;
        };
        auto put_float = [&](const char* key, float value)
        {
            ok = put_float_logged(prefs, "chat", key, value, emit_logs) && ok;
        };
        auto put_string = [&](const char* key, const char* value)
        {
            ok = put_string_logged(prefs, "chat", key, value, emit_logs) && ok;
        };
        auto put_bytes = [&](const char* key, const void* value, size_t len)
        {
            ok = put_bytes_logged(prefs, "chat", key, value, len, emit_logs) && ok;
        };

        // Save policy
        put_bool("relay", chat_policy.enable_relay);
        put_uchar("hop_limit", chat_policy.hop_limit_default);
        put_bool("ack_bcast", chat_policy.ack_for_broadcast);
        put_bool("ack_squad", chat_policy.ack_for_squad);
        put_uchar("max_retries", chat_policy.max_tx_retries);

        // Save meshtastic profile (legacy-compatible keys)
        put_uchar("region", meshtastic_config.region);
        put_uchar("modem_preset", meshtastic_config.modem_preset);
        put_char("tx_power", meshtastic_config.tx_power);
        put_uchar("mesh_hop_limit", meshtastic_config.hop_limit);
        put_bool("mesh_relay", meshtastic_config.enable_relay);
        put_bool("use_preset", meshtastic_config.use_preset);
        put_float("mesh_bw_khz", meshtastic_config.bandwidth_khz);
        put_uchar("mesh_sf", meshtastic_config.spread_factor);
        put_uchar("mesh_cr", meshtastic_config.coding_rate);
        put_bool("mesh_tx_en", meshtastic_config.tx_enabled);
        put_bool(kChatKeyMeshOverrideDuty, meshtastic_config.override_duty_cycle);
        put_ushort("mesh_ch_num", meshtastic_config.channel_num);
        put_float("mesh_freq_off", meshtastic_config.frequency_offset_mhz);
        put_float(kChatKeyMeshFreqOverride, meshtastic_config.override_frequency_mhz);
        put_bool(kChatKeyMeshIgnoreMqtt, meshtastic_config.ignore_mqtt);
        put_bool("mesh_ok_to_mqtt", meshtastic_config.config_ok_to_mqtt);

        // Save meshcore profile
        put_float("mc_freq", meshcore_config.meshcore_freq_mhz);
        put_float("mc_bw", meshcore_config.meshcore_bw_khz);
        put_uchar("mc_sf", meshcore_config.meshcore_sf);
        put_uchar("mc_cr", meshcore_config.meshcore_cr);
        put_uchar(kChatKeyMcRegionPreset, meshcore_config.meshcore_region_preset);
        put_char("mc_tx", meshcore_config.tx_power);
        put_bool("mc_repeat", meshcore_config.meshcore_client_repeat);
        put_float("mc_rx_delay", meshcore_config.meshcore_rx_delay_base);
        put_float("mc_airtime", meshcore_config.meshcore_airtime_factor);
        put_uchar("mc_flood_max", meshcore_config.meshcore_flood_max);
        put_bool("mc_multi_acks", meshcore_config.meshcore_multi_acks);
        put_uchar("mc_ch_slot", meshcore_config.meshcore_channel_slot);
        put_bool("mc_tx_en", meshcore_config.tx_enabled);
        put_string("mc_ch_name", meshcore_config.meshcore_channel_name);
        put_bytes("mc_ch_key", meshcore_config.secondary_key, sizeof(meshcore_config.secondary_key));

        put_float("rn_freq", rnode_config.override_frequency_mhz);
        put_float("rn_bw", rnode_config.bandwidth_khz);
        put_uchar("rn_sf", rnode_config.spread_factor);
        put_uchar("rn_cr", rnode_config.coding_rate);
        put_char("rn_tx", rnode_config.tx_power);
        put_bool("rn_tx_en", rnode_config.tx_enabled);
        // Remove first so legacy key type mismatches cannot block updating this value.
        ok = remove_key_logged(prefs, "chat", "mesh_protocol", emit_logs) && ok;
        put_uchar("mesh_protocol", static_cast<uint8_t>(mesh_protocol));

        // Save device name
        put_bytes("node_name", node_name, strlen(node_name));
        put_bytes("short_name", short_name, strlen(short_name));

        // Save channel settings
        put_bool("primary_enabled", primary_enabled);
        put_bool(kChatKeySecondaryEnabled, secondary_enabled);
        put_bool("primary_uplink", primary_uplink_enabled);
        put_bool(kChatKeyPrimaryDownlink, primary_downlink_enabled);
        put_bool(kChatKeySecondaryUplink, secondary_uplink_enabled);
        put_bool(kChatKeySecondaryDownlink, secondary_downlink_enabled);
        memcpy(secondary_key, meshtastic_config.secondary_key, sizeof(secondary_key));
        put_bytes("secondary_key", secondary_key, sizeof(secondary_key));
    }
    prefs.end();

    if (!begin_namespace(prefs, "gps", false, "SAVE", emit_logs))
    {
        return false;
    }
    {
        auto put_uchar = [&](const char* key, uint8_t value)
        {
            ok = put_uchar_logged(prefs, "gps", key, value, emit_logs) && ok;
        };
        auto put_uint = [&](const char* key, uint32_t value)
        {
            ok = put_uint_logged(prefs, "gps", key, value, emit_logs) && ok;
        };

        put_uint("gps_interval", gps_interval_ms);
        put_uchar("gps_mode", gps_mode);
        put_uchar("gps_sat_mask", gps_sat_mask);
        put_uchar("gps_strategy", gps_strategy);
        put_uchar("gps_alt_ref", gps_alt_ref);
        put_uchar("gps_coord_fmt", gps_coord_format);
        put_uint("motion_idle_ms", motion_config.idle_timeout_ms);
        put_uchar(kGpsKeyMotionSensorId, motion_config.sensor_id);
    }
    prefs.end();

    if (!begin_namespace(prefs, "settings", false, "SAVE", emit_logs))
    {
        return false;
    }
    {
        auto put_bool = [&](const char* key, bool value)
        {
            ok = put_bool_logged(prefs, "settings", key, value, emit_logs) && ok;
        };
        auto put_uchar = [&](const char* key, uint8_t value)
        {
            ok = put_uchar_logged(prefs, "settings", key, value, emit_logs) && ok;
        };
        auto put_string = [&](const char* key, const char* value)
        {
            ok = put_string_logged(prefs, "settings", key, value, emit_logs) && ok;
        };

        put_uchar("map_coord", map_coord_system);
        put_uchar("map_source", map_source);
        put_bool("map_contour", map_contour_enabled);
        put_bool("map_track", map_track_enabled);
        put_uchar(kSettingsKeyMapTrackInterval, map_track_interval);
        put_uchar(kSettingsKeyMapTrackFormat, map_track_format);
        put_bool("ble_enabled", ble_enabled);
        put_uchar("chat_channel", chat_channel);
        put_bool("net_duty_cycle", net_duty_cycle);
        put_uchar("net_util", net_channel_util);
        put_uchar("privacy_encrypt", privacy_encrypt_mode);
        ok = remove_key_logged(prefs, "settings", "privacy_pki", emit_logs) && ok;
        put_uchar("privacy_nmea", privacy_nmea_output);
        put_uchar(kSettingsKeyPrivacyNmeaSentence, privacy_nmea_sentence);
        put_bool("route_enabled", route_enabled);
        put_string("route_path", route_path);
    }
    prefs.end();

    if (!begin_namespace(prefs, "aprs", false, "SAVE", emit_logs))
    {
        return false;
    }
    {
        auto put_bool = [&](const char* key, bool value)
        {
            ok = put_bool_logged(prefs, "aprs", key, value, emit_logs) && ok;
        };
        auto put_uchar = [&](const char* key, uint8_t value)
        {
            ok = put_uchar_logged(prefs, "aprs", key, value, emit_logs) && ok;
        };
        auto put_char = [&](const char* key, int8_t value)
        {
            ok = put_char_logged(prefs, "aprs", key, value, emit_logs) && ok;
        };
        auto put_ushort = [&](const char* key, uint16_t value)
        {
            ok = put_ushort_logged(prefs, "aprs", key, value, emit_logs) && ok;
        };
        auto put_string = [&](const char* key, const char* value)
        {
            ok = put_string_logged(prefs, "aprs", key, value, emit_logs) && ok;
        };
        auto put_bytes = [&](const char* key, const void* value, size_t len)
        {
            ok = put_bytes_logged(prefs, "aprs", key, value, len, emit_logs) && ok;
        };

        put_bool("enabled", aprs.enabled);
        put_string("igate_call", aprs.igate_callsign);
        put_uchar("igate_ssid", aprs.igate_ssid);
        put_string("to_call", aprs.tocall);
        put_string("path", aprs.path);
        put_ushort("tx_min", aprs.tx_min_interval_s);
        put_ushort("dedupe", aprs.dedupe_window_s);
        put_char("sym_tab", aprs.symbol_table);
        put_char("sym_code", aprs.symbol_code);
        put_ushort("pos_interval", aprs.position_interval_s);
        if (aprs.node_map_len > 0)
        {
            put_bytes("node_map", aprs.node_map, aprs.node_map_len);
        }
        else
        {
            ok = remove_key_logged(prefs, "aprs", "node_map", emit_logs) && ok;
        }
        put_bool("self_enable", aprs.self_enable);
        put_string("self_call", aprs.self_callsign);
    }
    prefs.end();

    if (emit_logs)
    {
        Serial.printf("[AppCfg][SAVE] done ok=%s\n", bool_label(ok));
    }
    return ok;
}

uint8_t loadMessageToneVolume()
{
    int value = ::platform::ui::settings_store::get_int("settings", "speaker_volume", 45);
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
    return loadAppConfigFromPreferences(config, prefs, true);
}

bool saveAppConfig(const AppConfig& config)
{
    AppConfig previous_config;
    Preferences previous_prefs;
    loadAppConfigFromPreferences(previous_config, previous_prefs, false);
    log_config_delta(previous_config, config);

    Preferences prefs;
    AppConfig mutable_config = config;
    return saveAppConfigToPreferences(mutable_config, prefs, true);
}

} // namespace app
