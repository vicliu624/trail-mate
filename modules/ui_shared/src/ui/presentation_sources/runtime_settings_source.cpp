#include "ui/presentation_sources/runtime_settings_source.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/reticulum_network_config_runtime.h"
#include "platform/ui/screen_brightness_steps.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/timezone_profile.h"
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
#include "platform/ui/a7682e_cellular_runtime.h"
#endif
#include "sys/clock.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ui::presentation_sources
{
namespace
{

constexpr const char* kSettingsNamespace = "settings";
constexpr int kMqttPortChoices[] = {1883, 8883, 9001, 1884};

enum class Direction : int
{
    Previous = -1,
    None = 0,
    Next = 1,
};

bool keyEquals(const ui::settings::SettingsPatchView& patch, const char* key)
{
    return std::strcmp(patch.key.c_str(), key) == 0;
}

bool valueEquals(const ui::settings::SettingsPatchView& patch, const char* value)
{
    return std::strcmp(patch.value.c_str(), value) == 0;
}

bool parseBool(const char* text, bool& out)
{
    if (!text)
    {
        return false;
    }
    if (std::strcmp(text, "1") == 0 || std::strcmp(text, "true") == 0 ||
        std::strcmp(text, "TRUE") == 0 || std::strcmp(text, "on") == 0 ||
        std::strcmp(text, "ON") == 0)
    {
        out = true;
        return true;
    }
    if (std::strcmp(text, "0") == 0 || std::strcmp(text, "false") == 0 ||
        std::strcmp(text, "FALSE") == 0 || std::strcmp(text, "off") == 0 ||
        std::strcmp(text, "OFF") == 0)
    {
        out = false;
        return true;
    }
    return false;
}

Direction patchDirection(const ui::settings::SettingsPatchView& patch)
{
    if (valueEquals(patch, "next"))
    {
        return Direction::Next;
    }
    if (valueEquals(patch, "previous"))
    {
        return Direction::Previous;
    }
    return Direction::None;
}

bool adjustedBool(const ui::settings::SettingsPatchView& patch, bool current, bool& out)
{
    if (valueEquals(patch, "toggle"))
    {
        out = !current;
        return true;
    }
    return parseBool(patch.value.c_str(), out);
}

int cycleInt(int value, int minimum, int maximum, Direction direction)
{
    if (direction == Direction::None || minimum > maximum)
    {
        return value;
    }
    if (direction == Direction::Next)
    {
        return value >= maximum ? minimum : value + 1;
    }
    return value <= minimum ? maximum : value - 1;
}

template <size_t N>
int cycleChoice(int current, const int (&values)[N], Direction direction)
{
    if (N == 0 || direction == Direction::None)
    {
        return current;
    }
    size_t index = 0;
    for (; index < N; ++index)
    {
        if (values[index] == current)
        {
            break;
        }
    }
    if (index == N)
    {
        index = 0;
    }
    if (direction == Direction::Next)
    {
        index = (index + 1U) % N;
    }
    else
    {
        index = index == 0 ? N - 1U : index - 1U;
    }
    return values[index];
}

template <size_t N>
float cycleChoice(float current, const float (&values)[N], Direction direction)
{
    if (N == 0 || direction == Direction::None)
    {
        return current;
    }
    size_t index = 0;
    for (; index < N; ++index)
    {
        if (std::fabs(values[index] - current) < 0.01F)
        {
            break;
        }
    }
    if (index == N)
    {
        index = 0;
    }
    if (direction == Direction::Next)
    {
        index = (index + 1U) % N;
    }
    else
    {
        index = index == 0 ? N - 1U : index - 1U;
    }
    return values[index];
}

ui::settings::SettingsSection* addSection(ui::settings::SettingsSnapshot& out, const char* title)
{
    if (out.section_count >= (sizeof(out.sections) / sizeof(out.sections[0])))
    {
        return nullptr;
    }
    ui::settings::SettingsSection& section = out.sections[out.section_count++];
    ui::copyText(section.title, title);
    return &section;
}

void addOption(ui::settings::SettingsSection* section,
               const char* key,
               const char* label,
               const char* value,
               ui::settings::SettingControlKind control,
               bool enabled = true)
{
    if (!section || section->option_count >= (sizeof(section->options) / sizeof(section->options[0])))
    {
        return;
    }
    ui::settings::SettingsOption& option = section->options[section->option_count++];
    ui::copyText(option.key, key);
    ui::copyText(option.label, label);
    ui::copyText(option.value_label, value ? value : "");
    option.control = control;
    option.enabled = enabled;
}

void addBoolOption(ui::settings::SettingsSection* section,
                   const char* key,
                   const char* label,
                   bool value,
                   bool enabled = true)
{
    addOption(section,
              key,
              label,
              value ? "ON" : "OFF",
              ui::settings::SettingControlKind::Toggle,
              enabled);
}

void addUnsignedOption(ui::settings::SettingsSection* section,
                       const char* key,
                       const char* label,
                       unsigned long value,
                       ui::settings::SettingControlKind control =
                           ui::settings::SettingControlKind::Number)
{
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%lu", value);
    addOption(section, key, label, text, control);
}

void addSignedOption(ui::settings::SettingsSection* section,
                     const char* key,
                     const char* label,
                     int value,
                     ui::settings::SettingControlKind control =
                         ui::settings::SettingControlKind::Number)
{
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%d", value);
    addOption(section, key, label, text, control);
}

void addFloatOption(ui::settings::SettingsSection* section,
                    const char* key,
                    const char* label,
                    float value,
                    const char* suffix = "")
{
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.1f%s", static_cast<double>(value), suffix ? suffix : "");
    addOption(section, key, label, text, ui::settings::SettingControlKind::Choice);
}

const char* protocolLabel(chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case chat::MeshProtocol::Meshtastic:
        return "Meshtastic";
    case chat::MeshProtocol::MeshCore:
        return "MeshCore";
    case chat::MeshProtocol::Reticulum:
    case chat::MeshProtocol::RNode:
    default:
        return "Reticulum";
    }
}

uint8_t cycleMeshtasticRegion(uint8_t current, Direction direction)
{
    size_t count = 0;
    const auto* table = chat::meshtastic::getRegionTable(&count);
    if (!table || count == 0 || direction == Direction::None)
    {
        return current;
    }
    size_t index = 0;
    for (; index < count; ++index)
    {
        if (static_cast<uint8_t>(table[index].code) == current)
        {
            break;
        }
    }
    if (index >= count)
    {
        index = 0;
    }
    index = direction == Direction::Next ? (index + 1U) % count : (index == 0 ? count - 1U : index - 1U);
    return static_cast<uint8_t>(table[index].code);
}

uint8_t cycleMeshCoreRegionPreset(uint8_t current, Direction direction)
{
    size_t count = 0;
    const auto* table = chat::meshcore::getRegionPresetTable(&count);
    if (!table || count == 0 || direction == Direction::None)
    {
        return current;
    }
    size_t index = 0;
    for (; index < count; ++index)
    {
        if (table[index].id == current)
        {
            break;
        }
    }
    if (index >= count)
    {
        index = 0;
    }
    index = direction == Direction::Next ? (index + 1U) % count : (index == 0 ? count - 1U : index - 1U);
    return table[index].id;
}

void applyMeshCoreRegionPreset(chat::MeshConfig& mesh, uint8_t preset_id)
{
    mesh.meshcore_region_preset = preset_id;
    const auto* preset = chat::meshcore::findRegionPresetById(preset_id);
    if (!preset)
    {
        return;
    }
    mesh.meshcore_freq_mhz = preset->freq_mhz;
    mesh.meshcore_bw_khz = preset->bw_khz;
    mesh.meshcore_sf = preset->sf;
    mesh.meshcore_cr = preset->cr;
    mesh.tx_power = preset->tx_power_dbm;
}

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
size_t selected_tcp_slot = 0;
const chat::reticulum::NetworkInterfaceConfig* tcpEntry(size_t slot)
{
    const auto& network = platform::ui::reticulum_network_config::active();
    for (size_t i = 0; i < network.interface_count; ++i)
    {
        const auto& entry = network.interfaces[i];
        if (entry.type == chat::reticulum::NetworkInterfaceType::TcpClient && slot-- == 0) return &entry;
    }
    return nullptr;
}
#endif

void buildProfileSection(ui::settings::SettingsSnapshot& out, const app::AppConfig& config)
{
    ui::settings::SettingsSection* const section = addSection(out, "Profile");
    addOption(section,
              "node_name",
              "Node Name",
              config.node_name[0] ? config.node_name : "UNSET",
              ui::settings::SettingControlKind::Text);
    addOption(section,
              "short_name",
              "Short Name",
              config.short_name[0] ? config.short_name : "UNSET",
              ui::settings::SettingControlKind::Text);
    addOption(section,
              "mesh_protocol",
              "Protocol",
              protocolLabel(config.mesh_protocol),
              ui::settings::SettingControlKind::Choice);
    addOption(section,
              "chat_channel",
              "Chat Channel",
              config.chat_channel == 0 ? "Primary" : "Secondary",
              ui::settings::SettingControlKind::Choice);
}

void buildRadioSection(ui::settings::SettingsSnapshot& out, const app::AppConfig& config)
{
    ui::settings::SettingsSection* const section = addSection(out, "Radio");
    if (config.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        const auto& mesh = config.meshcore_config;
        const auto* preset = chat::meshcore::findRegionPresetById(mesh.meshcore_region_preset);
        addOption(section,
                  "mc_region_preset",
                  "Region Preset",
                  preset ? preset->title : "Custom",
                  ui::settings::SettingControlKind::Choice);
        addFloatOption(section, "mc_frequency", "Frequency", mesh.meshcore_freq_mhz, " MHz");
        addFloatOption(section, "mc_bandwidth", "Bandwidth", mesh.meshcore_bw_khz, " kHz");
        addUnsignedOption(section, "mc_sf", "Spread Factor", mesh.meshcore_sf,
                          ui::settings::SettingControlKind::Choice);
        addUnsignedOption(section, "mc_cr", "Coding Rate", mesh.meshcore_cr,
                          ui::settings::SettingControlKind::Choice);
        addSignedOption(section, "mc_power", "TX Power", mesh.tx_power,
                        ui::settings::SettingControlKind::Choice);
        addBoolOption(section, "mc_repeat", "Client Repeat", mesh.meshcore_client_repeat);
        addFloatOption(section, "mc_rx_delay", "RX Delay", mesh.meshcore_rx_delay_base, " s");
        addFloatOption(section, "mc_airtime", "Airtime Factor", mesh.meshcore_airtime_factor);
        addUnsignedOption(section, "mc_flood_max", "Flood Max", mesh.meshcore_flood_max,
                          ui::settings::SettingControlKind::Choice);
        addBoolOption(section, "mc_multi_acks", "Multi ACK", mesh.meshcore_multi_acks);
        addUnsignedOption(section, "mc_channel_slot", "Channel Slot", mesh.meshcore_channel_slot,
                          ui::settings::SettingControlKind::Choice);
        return;
    }

    if (config.mesh_protocol == chat::MeshProtocol::Reticulum ||
        config.mesh_protocol == chat::MeshProtocol::RNode)
    {
        const auto& mesh = config.reticulumConfig();
        const char* bearer = "All";
        if (mesh.reticulum_interface_policy == chat::ReticulumInterfacePolicy::LoRaOnly)
        {
            bearer = "LoRa";
        }
        else if (mesh.reticulum_interface_policy == chat::ReticulumInterfacePolicy::WifiGatewayOnly)
        {
            bearer = "Wi-Fi";
        }
        addOption(section, "rt_bearer", "Bearer", bearer, ui::settings::SettingControlKind::Choice);
        addBoolOption(section, "rt_lora", "LoRa", mesh.reticulum_lora_enabled);
        addBoolOption(section, "rt_wifi", "Wi-Fi Gateway", mesh.reticulum_wifi_gateway_enabled);
        addBoolOption(section, "rt_wifi_auto", "Wi-Fi Auto", mesh.reticulum_wifi_auto_connect);
        addBoolOption(section, "rt_anonymous", "Anonymous Peer", mesh.reticulum_anonymous_peer);
        addBoolOption(section, "rt_location", "Location Requests", mesh.reticulum_allow_location_requests);
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
        {
            const auto* entry = tcpEntry(selected_tcp_slot);
            char port[6];
            addUnsignedOption(section, "rt_tcp_slot", "TCP Entry", selected_tcp_slot + 1, ui::settings::SettingControlKind::Choice);
            addOption(section, "rt_wifi_host", "Gateway Host", entry ? entry->target_host : "", ui::settings::SettingControlKind::Text);
            std::snprintf(port, sizeof(port), "%u", static_cast<unsigned>(entry ? entry->target_port : 4242));
            addOption(section, "rt_wifi_port", "Gateway Port", port, ui::settings::SettingControlKind::Text);
        }
#else
        addOption(section,
                  "rt_wifi_host",
                  "Gateway Host",
                  mesh.reticulum_wifi_gateway_host[0] ? mesh.reticulum_wifi_gateway_host : "UNSET",
                  ui::settings::SettingControlKind::Text);
        addUnsignedOption(section, "rt_wifi_port", "Gateway Port", mesh.reticulum_wifi_gateway_port);
#endif
        addSignedOption(section, "rt_power", "TX Power", mesh.tx_power,
                        ui::settings::SettingControlKind::Choice);
        addBoolOption(section, "rt_tx", "Transmit", mesh.tx_enabled);
        return;
    }

    const auto& mesh = config.meshtastic_config;
    const auto* region = chat::meshtastic::findRegion(
        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(mesh.region));
    addOption(section,
              "mt_region",
              "Region",
              region ? region->label : "Unknown",
              ui::settings::SettingControlKind::Choice);
    addBoolOption(section, "mt_use_preset", "Use Preset", mesh.use_preset);
    addOption(section,
              "mt_modem_preset",
              "Modem Preset",
              chat::meshtastic::presetDisplayName(
                  static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(mesh.modem_preset)),
              ui::settings::SettingControlKind::Choice);
    addFloatOption(section, "mt_bandwidth", "Bandwidth", mesh.bandwidth_khz, " kHz");
    addUnsignedOption(section, "mt_sf", "Spread Factor", mesh.spread_factor,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "mt_cr", "Coding Rate", mesh.coding_rate,
                      ui::settings::SettingControlKind::Choice);
    addSignedOption(section, "mt_power", "TX Power", mesh.tx_power,
                    ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "mt_hop_limit", "Hop Limit", mesh.hop_limit,
                      ui::settings::SettingControlKind::Choice);
    addBoolOption(section, "mt_tx", "Transmit", mesh.tx_enabled);
    addBoolOption(section, "mt_override_duty", "Override Duty", mesh.override_duty_cycle);
    addUnsignedOption(section, "mt_channel_num", "Channel No.", mesh.channel_num,
                      ui::settings::SettingControlKind::Choice);
    addBoolOption(section, "mt_ignore_mqtt", "Ignore MQTT In", mesh.ignore_mqtt);
}

void buildChannelsSection(ui::settings::SettingsSnapshot& out, const app::AppConfig& config)
{
    ui::settings::SettingsSection* const section = addSection(out, "Channels");
    addBoolOption(section, "primary_enabled", "Primary Enabled", config.primary_enabled);
    addBoolOption(section, "secondary_enabled", "Secondary Enabled", config.secondary_enabled);
    addBoolOption(section, "primary_uplink", "Primary Uplink", config.primary_uplink_enabled);
    addBoolOption(section, "primary_downlink", "Primary Downlink", config.primary_downlink_enabled);
    addBoolOption(section, "secondary_uplink", "Secondary Uplink", config.secondary_uplink_enabled);
    addBoolOption(section, "secondary_downlink", "Secondary Downlink", config.secondary_downlink_enabled);
}

void buildChatSection(ui::settings::SettingsSnapshot& out, const app::AppConfig& config)
{
    ui::settings::SettingsSection* const section = addSection(out, "Chat");
    addBoolOption(section, "chat_relay", "Relay", config.chat_policy.enable_relay);
    addUnsignedOption(section, "chat_hop_default", "Default Hop", config.chat_policy.hop_limit_default,
                      ui::settings::SettingControlKind::Choice);
    addBoolOption(section, "chat_ack_broadcast", "Broadcast ACK", config.chat_policy.ack_for_broadcast);
    addBoolOption(section, "chat_ack_squad", "Squad ACK", config.chat_policy.ack_for_squad);
    addUnsignedOption(section, "chat_retries", "TX Retries", config.chat_policy.max_tx_retries,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "chat_max_channels", "Max Channels", config.chat_policy.max_channels,
                      ui::settings::SettingControlKind::Choice);
}

void buildGpsSection(ui::settings::SettingsSnapshot& out, const app::AppConfig& config)
{
    ui::settings::SettingsSection* const section = addSection(out, "GPS");
    addBoolOption(section, "gps_enabled", "GPS Enabled", config.gps_enabled);
    addUnsignedOption(section, "gps_init_baud", "Receiver Baud", config.gps_init_baud,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_init_probe", "Probe Window", config.gps_init_probe_ms,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_init_profile", "Receiver Profile", config.gps_init_profile,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_init_rxm", "RXM Init", config.gps_init_rxm_policy,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_init_gnss", "GNSS Init", config.gps_init_gnss_policy,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_init_nmea", "NMEA Init", config.gps_init_nmea_policy,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_mode", "Location Mode", config.gps_mode,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_sat_mask", "Satellite Mask", config.gps_sat_mask,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_strategy", "Position Strategy", config.gps_strategy,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_interval", "Update Interval", config.gps_interval_ms / 1000U,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "gps_coord_format", "Coordinate Format", config.gps_coord_format,
                      ui::settings::SettingControlKind::Choice);
}

void buildMapSection(ui::settings::SettingsSnapshot& out, const app::AppConfig& config)
{
    ui::settings::SettingsSection* const section = addSection(out, "Map");
    const char* map_source = config.map_source == 1 ? "Terrain" : config.map_source == 2 ? "Satellite"
                                                                                         : "OSM";
    addOption(section, "map_source", "Base Map", map_source, ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "map_coord_system", "Coordinate System", config.map_coord_system,
                      ui::settings::SettingControlKind::Choice);
    addBoolOption(section, "map_contour", "Contour Overlay", config.map_contour_enabled);
    addBoolOption(section, "map_track", "Track Recording", config.map_track_enabled);
    addUnsignedOption(section, "map_track_interval", "Track Interval", config.map_track_interval,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "map_track_format", "Track Format", config.map_track_format,
                      ui::settings::SettingControlKind::Choice);
}

void buildNetworkSection(ui::settings::SettingsSnapshot& out, const app::AppConfig& config)
{
    ui::settings::SettingsSection* const section = addSection(out, "Network");
    addBoolOption(section, "net_duty_cycle", "Duty Limits", config.net_duty_cycle);
    addUnsignedOption(section, "net_channel_util", "Channel Util", config.net_channel_util,
                      ui::settings::SettingControlKind::Choice);
    addUnsignedOption(section, "privacy_encrypt", "Encryption", config.privacy_encrypt_mode,
                      ui::settings::SettingControlKind::Choice);

    if (config.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        const auto& mesh = config.meshcore_config;
        addBoolOption(section, "mqtt_enabled", "MQTT Enabled", mesh.meshcore_mqtt_enabled);
        addBoolOption(section, "mqtt_uplink", "MQTT Uplink", mesh.meshcore_mqtt_uplink_enabled);
        addBoolOption(section, "mqtt_downlink", "MQTT Downlink", mesh.meshcore_mqtt_downlink_enabled);
        addOption(section, "mqtt_host", "MQTT Host", mesh.meshcore_mqtt_host,
                  ui::settings::SettingControlKind::Text);
        addUnsignedOption(section, "mqtt_port", "MQTT Port", mesh.meshcore_mqtt_port);
        addOption(section, "mqtt_root", "MQTT Root", mesh.meshcore_mqtt_root,
                  ui::settings::SettingControlKind::Text);
        addOption(section, "mqtt_user", "MQTT User", mesh.meshcore_mqtt_username,
                  ui::settings::SettingControlKind::Text);
        addOption(section, "mqtt_password", "MQTT Password", "HIDDEN",
                  ui::settings::SettingControlKind::Text);
        return;
    }

    addBoolOption(section, "mqtt_enabled", "MQTT Enabled", config.meshtastic_mqtt_enabled);
    addBoolOption(section, "mqtt_uplink", "MQTT Uplink", config.meshtastic_mqtt_uplink_enabled);
    addBoolOption(section, "mqtt_downlink", "MQTT Downlink", config.meshtastic_mqtt_downlink_enabled);
    addOption(section, "mqtt_host", "MQTT Host", config.meshtastic_mqtt_host,
              ui::settings::SettingControlKind::Text);
    addUnsignedOption(section, "mqtt_port", "MQTT Port", config.meshtastic_mqtt_port);
    addOption(section, "mqtt_root", "MQTT Root", config.meshtastic_mqtt_root,
              ui::settings::SettingControlKind::Text);
    addOption(section, "mqtt_user", "MQTT User", config.meshtastic_mqtt_username,
              ui::settings::SettingControlKind::Text);
    addOption(section, "mqtt_password", "MQTT Password", "HIDDEN",
              ui::settings::SettingControlKind::Text);
}

void buildDeviceSection(ui::settings::SettingsSnapshot& out)
{
    ui::settings::SettingsSection* const section = addSection(out, "Device");
    const uint8_t brightness_max = platform::ui::device::screen_brightness_max();
    const uint8_t brightness = platform::ui::screen_brightness_steps::clampLevel(
        platform::ui::settings_store::get_int(kSettingsNamespace,
                                              "screen_brightness",
                                              platform::ui::device::screen_brightness()),
        brightness_max);
    const unsigned brightness_percent = brightness_max == 0
                                            ? 0U
                                            : static_cast<unsigned>((static_cast<unsigned>(brightness) * 100U +
                                                                     (brightness_max / 2U)) /
                                                                    brightness_max);
    char brightness_text[16] = {};
    std::snprintf(brightness_text, sizeof(brightness_text), "%u%%", brightness_percent);
    addOption(section, "screen_brightness", "Brightness", brightness_text,
              ui::settings::SettingControlKind::Choice,
              platform::ui::device::supports_screen_brightness());

    const int speaker_volume = platform::ui::settings_store::get_int(kSettingsNamespace, "speaker_volume", 45);
    addUnsignedOption(section, "speaker_volume", "Speaker Volume", static_cast<unsigned long>(speaker_volume),
                      ui::settings::SettingControlKind::Choice);
    addBoolOption(section,
                  "vibration_enabled",
                  "Vibration",
                  platform::ui::settings_store::get_bool(kSettingsNamespace, "vibration_enabled", true));

    const auto* profile = platform::ui::time::timezone_profile_by_id(
        platform::ui::time::timezone_profile_id());
    addOption(section,
              "timezone_profile",
              "Time Zone",
              profile ? profile->label : "UTC",
              ui::settings::SettingControlKind::Choice);

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    const auto& cellular = ::platform::ui::a7682e::status();
    addBoolOption(section, "cellular_enabled", "4G Enabled", cellular.enabled);
    addOption(section,
              "cellular_config",
              "4G Configuration",
              "OPEN",
              ui::settings::SettingControlKind::Action);
#endif
}

template <typename Mutator>
ui::UiActionResult updateConfig(app::AppConfigChangeSet changes, Mutator&& mutator)
{
    app::IAppFacade& app_ctx = app::appFacade();
    auto edit = app_ctx.beginConfigEdit();
    if (!edit)
    {
        return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }
    mutator(edit.config());
    edit.commit(changes);
    return ui::UiActionResult::success();
}

template <typename Mutator>
ui::UiActionResult updateMeshConfig(Mutator&& mutator)
{
    const ui::UiActionResult result = updateConfig(app::AppConfigChangeSet::mesh(), mutator);
    if (result.ok)
    {
        app::appFacade().applyMeshConfig();
    }
    return result;
}

bool patchTextIsUsable(const ui::settings::SettingsPatchView& patch)
{
    return patch.value.c_str()[0] != '\0' && patchDirection(patch) == Direction::None &&
           !valueEquals(patch, "toggle");
}

void copyBoundedText(char* destination, std::size_t destination_size, const char* source)
{
    if (!destination || destination_size == 0U)
    {
        return;
    }

    const char* const value = source ? source : "";
    const std::size_t maximum_copy_length = destination_size - 1U;
    const std::size_t source_length = std::strlen(value);
    const std::size_t copy_length = source_length < maximum_copy_length
                                        ? source_length
                                        : maximum_copy_length;
    std::memcpy(destination, value, copy_length);
    destination[copy_length] = '\0';
}

} // namespace

bool RuntimeSettingsSource::buildSettingsSnapshot(
    ui::settings::SettingsSnapshot& out) const
{
    out = ui::settings::SettingsSnapshot{};
    out.header.valid = true;
    out.header.version = 2;
    out.header.generated_at_ms = sys::millis_now();

    const app::AppConfig& config = app::configFacade().readConfig();
    buildProfileSection(out, config);
    buildRadioSection(out, config);
    buildChannelsSection(out, config);
    buildChatSection(out, config);
    buildGpsSection(out, config);
    buildMapSection(out, config);
    buildNetworkSection(out, config);
    buildDeviceSection(out);
    return true;
}

ui::UiActionResult RuntimeSettingsActionSink::applySetting(
    const ui::settings::SettingsPatchView& patch)
{
    const Direction direction = patchDirection(patch);
    const app::AppConfig& current = app::configFacade().readConfig();

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
    if (keyEquals(patch, "rt_tcp_slot"))
    {
        if (direction == Direction::None) return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        selected_tcp_slot = static_cast<size_t>(cycleInt(static_cast<int>(selected_tcp_slot), 0, 2, direction));
        return ui::UiActionResult::success();
    }
    for (size_t slot = 0; slot < chat::reticulum::kMaxTcpClientInterfaces; ++slot)
    {
        char host_key[24], port_key[24];
        std::snprintf(host_key, sizeof(host_key), "rt_tcp_%u_host", static_cast<unsigned>(slot + 1));
        std::snprintf(port_key, sizeof(port_key), "rt_tcp_%u_port", static_cast<unsigned>(slot + 1));
        const bool host = keyEquals(patch, host_key) || (slot == selected_tcp_slot && keyEquals(patch, "rt_wifi_host"));
        const bool port = keyEquals(patch, port_key) || (slot == selected_tcp_slot && keyEquals(patch, "rt_wifi_port"));
        if (!host && !port) continue;
        const auto* entry = tcpEntry(slot);
        char target[chat::reticulum::kInterfaceHostMaxLen + 1]{};
        std::snprintf(target, sizeof(target), "%s", entry ? entry->target_host : "");
        long number = entry ? entry->target_port : 4242;
        if (port)
        {
            char* end = nullptr;
            number = std::strtol(patch.value.c_str(), &end, 10);
            if (!end || end == patch.value.c_str() || *end || number < 1 || number > 65535)
                return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        if (!platform::ui::reticulum_network_config::updateTcpEndpoint(
                slot, host ? patch.value.c_str() : target, static_cast<uint16_t>(number)))
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        auto& facade = app::appFacade();
        facade.requestSaveConfig(app::AppConfigChangeSet::mesh());
        facade.applyMeshConfig();
        return ui::UiActionResult::success();
    }
#endif

    if (keyEquals(patch, "node_name") || keyEquals(patch, "short_name"))
    {
        if (!patchTextIsUsable(patch))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        const bool node_name = keyEquals(patch, "node_name");
        const ui::UiActionResult result = updateConfig(
            app::AppConfigChangeSet::identity(),
            [&](app::AppConfig& config)
            {
                char* out = node_name ? config.node_name : config.short_name;
                const size_t length = node_name ? sizeof(config.node_name) : sizeof(config.short_name);
                copyBoundedText(out, length, patch.value.c_str());
            });
        if (result.ok)
        {
            app::appFacade().applyUserInfo();
        }
        return result;
    }

    if (keyEquals(patch, "mesh_protocol"))
    {
        if (direction == Direction::None)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        constexpr chat::MeshProtocol kProtocols[] = {
            chat::MeshProtocol::Meshtastic,
            chat::MeshProtocol::MeshCore,
            chat::MeshProtocol::Reticulum,
        };
        size_t index = 0;
        for (; index < sizeof(kProtocols) / sizeof(kProtocols[0]); ++index)
        {
            if (kProtocols[index] == current.mesh_protocol)
            {
                break;
            }
        }
        if (index >= sizeof(kProtocols) / sizeof(kProtocols[0]))
        {
            index = 0;
        }
        index = direction == Direction::Next
                    ? (index + 1U) % (sizeof(kProtocols) / sizeof(kProtocols[0]))
                    : (index == 0 ? (sizeof(kProtocols) / sizeof(kProtocols[0])) - 1U : index - 1U);
        return updateMeshConfig([&](app::AppConfig& config)
                                { config.mesh_protocol = kProtocols[index]; });
    }

    if (keyEquals(patch, "chat_channel"))
    {
        if (direction == Direction::None)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        const ui::UiActionResult result = updateConfig(
            app::AppConfigChangeSet::chatUi(),
            [direction](app::AppConfig& config)
            {
                config.chat_channel = static_cast<uint8_t>(cycleInt(config.chat_channel, 0, 1, direction));
            });
        if (result.ok)
        {
            app::appFacade().applyChatDefaults();
        }
        return result;
    }

    if (keyEquals(patch, "mt_region") || keyEquals(patch, "mt_use_preset") ||
        keyEquals(patch, "mt_modem_preset") || keyEquals(patch, "mt_bandwidth") ||
        keyEquals(patch, "mt_sf") || keyEquals(patch, "mt_cr") ||
        keyEquals(patch, "mt_power") || keyEquals(patch, "mt_hop_limit") ||
        keyEquals(patch, "mt_tx") || keyEquals(patch, "mt_override_duty") ||
        keyEquals(patch, "mt_channel_num") || keyEquals(patch, "mt_ignore_mqtt"))
    {
        if (direction == Direction::None && !valueEquals(patch, "toggle"))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        return updateMeshConfig(
            [&](app::AppConfig& config)
            {
                auto& mesh = config.meshtastic_config;
                bool value = false;
                if (keyEquals(patch, "mt_region"))
                {
                    mesh.region = cycleMeshtasticRegion(mesh.region, direction);
                }
                else if (keyEquals(patch, "mt_use_preset") && adjustedBool(patch, mesh.use_preset, value))
                {
                    mesh.use_preset = value;
                }
                else if (keyEquals(patch, "mt_modem_preset"))
                {
                    mesh.modem_preset = static_cast<uint8_t>(cycleInt(mesh.modem_preset, 0, 6, direction));
                    mesh.use_preset = true;
                }
                else if (keyEquals(patch, "mt_bandwidth"))
                {
                    constexpr float values[] = {62.5F, 125.0F, 250.0F, 500.0F};
                    mesh.bandwidth_khz = cycleChoice(mesh.bandwidth_khz, values, direction);
                    mesh.use_preset = false;
                }
                else if (keyEquals(patch, "mt_sf"))
                {
                    mesh.spread_factor = static_cast<uint8_t>(cycleInt(mesh.spread_factor, 5, 12, direction));
                    mesh.use_preset = false;
                }
                else if (keyEquals(patch, "mt_cr"))
                {
                    mesh.coding_rate = static_cast<uint8_t>(cycleInt(mesh.coding_rate, 5, 8, direction));
                    mesh.use_preset = false;
                }
                else if (keyEquals(patch, "mt_power"))
                {
                    mesh.tx_power = static_cast<int8_t>(cycleInt(mesh.tx_power,
                                                                 app::AppConfig::kTxPowerMinDbm,
                                                                 app::AppConfig::kTxPowerMaxDbm,
                                                                 direction));
                }
                else if (keyEquals(patch, "mt_hop_limit"))
                {
                    mesh.hop_limit = static_cast<uint8_t>(cycleInt(mesh.hop_limit, 1, 7, direction));
                }
                else if (keyEquals(patch, "mt_tx") && adjustedBool(patch, mesh.tx_enabled, value))
                {
                    mesh.tx_enabled = value;
                }
                else if (keyEquals(patch, "mt_override_duty") && adjustedBool(patch, mesh.override_duty_cycle, value))
                {
                    mesh.override_duty_cycle = value;
                }
                else if (keyEquals(patch, "mt_channel_num"))
                {
                    mesh.channel_num = static_cast<uint16_t>(cycleInt(mesh.channel_num, 0, 15, direction));
                }
                else if (keyEquals(patch, "mt_ignore_mqtt") && adjustedBool(patch, mesh.ignore_mqtt, value))
                {
                    mesh.ignore_mqtt = value;
                }
            });
    }

    if (keyEquals(patch, "mc_region_preset") || keyEquals(patch, "mc_frequency") ||
        keyEquals(patch, "mc_bandwidth") || keyEquals(patch, "mc_sf") ||
        keyEquals(patch, "mc_cr") || keyEquals(patch, "mc_power") ||
        keyEquals(patch, "mc_repeat") || keyEquals(patch, "mc_rx_delay") ||
        keyEquals(patch, "mc_airtime") || keyEquals(patch, "mc_flood_max") ||
        keyEquals(patch, "mc_multi_acks") || keyEquals(patch, "mc_channel_slot"))
    {
        if (direction == Direction::None && !valueEquals(patch, "toggle"))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        return updateMeshConfig(
            [&](app::AppConfig& config)
            {
                auto& mesh = config.meshcore_config;
                bool value = false;
                if (keyEquals(patch, "mc_region_preset"))
                {
                    applyMeshCoreRegionPreset(mesh,
                                              cycleMeshCoreRegionPreset(mesh.meshcore_region_preset, direction));
                }
                else if (keyEquals(patch, "mc_frequency"))
                {
                    constexpr float values[] = {433.920F, 868.125F, 869.525F, 915.000F};
                    mesh.meshcore_freq_mhz = cycleChoice(mesh.meshcore_freq_mhz, values, direction);
                    mesh.meshcore_region_preset = 0;
                }
                else if (keyEquals(patch, "mc_bandwidth"))
                {
                    constexpr float values[] = {62.5F, 125.0F, 250.0F, 500.0F};
                    mesh.meshcore_bw_khz = cycleChoice(mesh.meshcore_bw_khz, values, direction);
                    mesh.meshcore_region_preset = 0;
                }
                else if (keyEquals(patch, "mc_sf"))
                {
                    mesh.meshcore_sf = static_cast<uint8_t>(cycleInt(mesh.meshcore_sf, 5, 12, direction));
                    mesh.meshcore_region_preset = 0;
                }
                else if (keyEquals(patch, "mc_cr"))
                {
                    mesh.meshcore_cr = static_cast<uint8_t>(cycleInt(mesh.meshcore_cr, 5, 8, direction));
                    mesh.meshcore_region_preset = 0;
                }
                else if (keyEquals(patch, "mc_power"))
                {
                    mesh.tx_power = static_cast<int8_t>(cycleInt(mesh.tx_power,
                                                                 app::AppConfig::kTxPowerMinDbm,
                                                                 app::AppConfig::kTxPowerMaxDbm,
                                                                 direction));
                }
                else if (keyEquals(patch, "mc_repeat") && adjustedBool(patch, mesh.meshcore_client_repeat, value))
                {
                    mesh.meshcore_client_repeat = value;
                }
                else if (keyEquals(patch, "mc_rx_delay"))
                {
                    constexpr float values[] = {0.0F, 0.5F, 1.0F, 2.0F, 5.0F};
                    mesh.meshcore_rx_delay_base = cycleChoice(mesh.meshcore_rx_delay_base, values, direction);
                }
                else if (keyEquals(patch, "mc_airtime"))
                {
                    constexpr float values[] = {0.5F, 1.0F, 1.5F, 2.0F, 5.0F};
                    mesh.meshcore_airtime_factor = cycleChoice(mesh.meshcore_airtime_factor, values, direction);
                }
                else if (keyEquals(patch, "mc_flood_max"))
                {
                    constexpr int values[] = {8, 16, 32, 64, 128};
                    mesh.meshcore_flood_max = static_cast<uint8_t>(cycleChoice(mesh.meshcore_flood_max, values, direction));
                }
                else if (keyEquals(patch, "mc_multi_acks") && adjustedBool(patch, mesh.meshcore_multi_acks, value))
                {
                    mesh.meshcore_multi_acks = value;
                }
                else if (keyEquals(patch, "mc_channel_slot"))
                {
                    mesh.meshcore_channel_slot = static_cast<uint8_t>(cycleInt(mesh.meshcore_channel_slot, 0, 7, direction));
                }
            });
    }

    if (keyEquals(patch, "rt_bearer") || keyEquals(patch, "rt_lora") ||
        keyEquals(patch, "rt_wifi") || keyEquals(patch, "rt_wifi_auto") ||
        keyEquals(patch, "rt_anonymous") || keyEquals(patch, "rt_location") ||
        keyEquals(patch, "rt_wifi_host") || keyEquals(patch, "rt_wifi_port") ||
        keyEquals(patch, "rt_power") || keyEquals(patch, "rt_tx"))
    {
        const bool text_host = keyEquals(patch, "rt_wifi_host");
        if ((text_host && !patchTextIsUsable(patch)) ||
            (!text_host && direction == Direction::None && !valueEquals(patch, "toggle")))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        return updateMeshConfig(
            [&](app::AppConfig& config)
            {
                auto& mesh = config.reticulumConfig();
                bool value = false;
                if (keyEquals(patch, "rt_bearer"))
                {
                    mesh.reticulum_interface_policy = static_cast<chat::ReticulumInterfacePolicy>(
                        cycleInt(static_cast<int>(mesh.reticulum_interface_policy), 0, 2, direction));
                }
                else if (keyEquals(patch, "rt_lora") && adjustedBool(patch, mesh.reticulum_lora_enabled, value)) mesh.reticulum_lora_enabled = value;
                else if (keyEquals(patch, "rt_wifi") && adjustedBool(patch, mesh.reticulum_wifi_gateway_enabled, value)) mesh.reticulum_wifi_gateway_enabled = value;
                else if (keyEquals(patch, "rt_wifi_auto") && adjustedBool(patch, mesh.reticulum_wifi_auto_connect, value)) mesh.reticulum_wifi_auto_connect = value;
                else if (keyEquals(patch, "rt_anonymous") && adjustedBool(patch, mesh.reticulum_anonymous_peer, value)) mesh.reticulum_anonymous_peer = value;
                else if (keyEquals(patch, "rt_location") && adjustedBool(patch, mesh.reticulum_allow_location_requests, value)) mesh.reticulum_allow_location_requests = value;
                else if (keyEquals(patch, "rt_wifi_host")) std::snprintf(mesh.reticulum_wifi_gateway_host, sizeof(mesh.reticulum_wifi_gateway_host), "%s", patch.value.c_str());
                else if (keyEquals(patch, "rt_wifi_port"))
                {
                    constexpr int values[] = {4242, 4243, 4244};
                    mesh.reticulum_wifi_gateway_port = static_cast<uint16_t>(cycleChoice(static_cast<int>(mesh.reticulum_wifi_gateway_port), values, direction));
                }
                else if (keyEquals(patch, "rt_power")) mesh.tx_power = static_cast<int8_t>(cycleInt(mesh.tx_power, app::AppConfig::kTxPowerMinDbm, app::AppConfig::kTxPowerMaxDbm, direction));
                else if (keyEquals(patch, "rt_tx") && adjustedBool(patch, mesh.tx_enabled, value)) mesh.tx_enabled = value;
            });
    }

    if (keyEquals(patch, "gps_enabled") || keyEquals(patch, "gps.enabled"))
    {
        bool enabled = false;
        if (!adjustedBool(patch, current.gps_enabled, enabled))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        const ui::UiActionResult result = updateConfig(
            app::AppConfigChangeSet::gps(),
            [enabled](app::AppConfig& config)
            { config.gps_enabled = enabled; });
        if (result.ok)
        {
            platform::ui::gps::set_enabled(enabled);
            app::appFacade().applyPositionConfig();
        }
        return result;
    }

    if (keyEquals(patch, "gps_init_baud") || keyEquals(patch, "gps_init_probe") ||
        keyEquals(patch, "gps_init_profile") || keyEquals(patch, "gps_init_rxm") ||
        keyEquals(patch, "gps_init_gnss") || keyEquals(patch, "gps_init_nmea") ||
        keyEquals(patch, "gps_mode") || keyEquals(patch, "gps_sat_mask") ||
        keyEquals(patch, "gps_strategy") || keyEquals(patch, "gps_interval") ||
        keyEquals(patch, "gps_coord_format"))
    {
        if (direction == Direction::None)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        const ui::UiActionResult result = updateConfig(
            app::AppConfigChangeSet::gps(),
            [&](app::AppConfig& config)
            {
                if (keyEquals(patch, "gps_init_baud"))
                {
                    constexpr int values[] = {0, 9600, 38400, 57600, 115200};
                    config.gps_init_baud = static_cast<uint32_t>(cycleChoice(
                        static_cast<int>(config.gps_init_baud), values, direction));
                }
                else if (keyEquals(patch, "gps_init_probe"))
                {
                    constexpr int values[] = {300, 600, 900, 1500, 2500};
                    config.gps_init_probe_ms = static_cast<uint32_t>(cycleChoice(
                        static_cast<int>(config.gps_init_probe_ms), values, direction));
                }
                else if (keyEquals(patch, "gps_init_profile"))
                {
                    config.gps_init_profile = static_cast<uint8_t>(cycleInt(config.gps_init_profile, 0, 3, direction));
                }
                else if (keyEquals(patch, "gps_init_rxm"))
                {
                    config.gps_init_rxm_policy = static_cast<uint8_t>(cycleInt(config.gps_init_rxm_policy, 0, 2, direction));
                }
                else if (keyEquals(patch, "gps_init_gnss"))
                {
                    config.gps_init_gnss_policy = static_cast<uint8_t>(cycleInt(config.gps_init_gnss_policy, 0, 2, direction));
                }
                else if (keyEquals(patch, "gps_init_nmea"))
                {
                    config.gps_init_nmea_policy = static_cast<uint8_t>(cycleInt(config.gps_init_nmea_policy, 0, 2, direction));
                }
                else if (keyEquals(patch, "gps_mode"))
                {
                    config.gps_mode = static_cast<uint8_t>(cycleInt(config.gps_mode, 0, 2, direction));
                }
                else if (keyEquals(patch, "gps_sat_mask"))
                {
                    constexpr int values[] = {1, 5, 9, 13, 15};
                    config.gps_sat_mask = static_cast<uint8_t>(cycleChoice(config.gps_sat_mask, values, direction));
                }
                else if (keyEquals(patch, "gps_strategy"))
                {
                    config.gps_strategy = static_cast<uint8_t>(cycleInt(config.gps_strategy, 0, 2, direction));
                }
                else if (keyEquals(patch, "gps_interval"))
                {
                    constexpr int values[] = {10, 30, 60, 300, 900};
                    config.gps_interval_ms = static_cast<uint32_t>(cycleChoice(
                                                 static_cast<int>(config.gps_interval_ms / 1000U), values, direction)) *
                                             1000U;
                }
                else
                {
                    config.gps_coord_format = static_cast<uint8_t>(cycleInt(config.gps_coord_format, 0, 2, direction));
                }
            });
        if (result.ok)
        {
            app::appFacade().applyPositionConfig();
        }
        return result;
    }

    if (keyEquals(patch, "map_source") || keyEquals(patch, "map_coord_system") ||
        keyEquals(patch, "map_contour") || keyEquals(patch, "map_track") ||
        keyEquals(patch, "map_track_interval") || keyEquals(patch, "map_track_format"))
    {
        const ui::UiActionResult result = updateConfig(
            app::AppConfigChangeSet::map(),
            [&](app::AppConfig& config)
            {
                if (keyEquals(patch, "map_contour"))
                {
                    bool value = false;
                    if (adjustedBool(patch, config.map_contour_enabled, value))
                    {
                        config.map_contour_enabled = value;
                    }
                }
                else if (keyEquals(patch, "map_track"))
                {
                    bool value = false;
                    if (adjustedBool(patch, config.map_track_enabled, value))
                    {
                        config.map_track_enabled = value;
                    }
                }
                else if (keyEquals(patch, "map_source"))
                {
                    config.map_source = static_cast<uint8_t>(cycleInt(config.map_source, 0, 2, direction));
                }
                else if (keyEquals(patch, "map_coord_system"))
                {
                    config.map_coord_system = static_cast<uint8_t>(cycleInt(config.map_coord_system, 0, 2, direction));
                }
                else if (keyEquals(patch, "map_track_interval"))
                {
                    constexpr int values[] = {1, 5, 15, 60, 99};
                    config.map_track_interval = static_cast<uint8_t>(cycleChoice(config.map_track_interval, values, direction));
                }
                else if (keyEquals(patch, "map_track_format"))
                {
                    config.map_track_format = static_cast<uint8_t>(cycleInt(config.map_track_format, 0, 2, direction));
                }
            });
        return result;
    }

    if (keyEquals(patch, "primary_enabled") || keyEquals(patch, "secondary_enabled") ||
        keyEquals(patch, "primary_uplink") || keyEquals(patch, "primary_downlink") ||
        keyEquals(patch, "secondary_uplink") || keyEquals(patch, "secondary_downlink"))
    {
        const ui::UiActionResult result = updateConfig(
            app::AppConfigChangeSet::channels(),
            [&](app::AppConfig& config)
            {
                bool value = false;
                if (keyEquals(patch, "primary_enabled") && adjustedBool(patch, config.primary_enabled, value))
                    config.primary_enabled = value;
                else if (keyEquals(patch, "secondary_enabled") && adjustedBool(patch, config.secondary_enabled, value))
                    config.secondary_enabled = value;
                else if (keyEquals(patch, "primary_uplink") && adjustedBool(patch, config.primary_uplink_enabled, value))
                    config.primary_uplink_enabled = value;
                else if (keyEquals(patch, "primary_downlink") && adjustedBool(patch, config.primary_downlink_enabled, value))
                    config.primary_downlink_enabled = value;
                else if (keyEquals(patch, "secondary_uplink") && adjustedBool(patch, config.secondary_uplink_enabled, value))
                    config.secondary_uplink_enabled = value;
                else if (keyEquals(patch, "secondary_downlink") && adjustedBool(patch, config.secondary_downlink_enabled, value))
                    config.secondary_downlink_enabled = value;
            });
        if (result.ok)
        {
            app::appFacade().applyMeshConfig();
        }
        return result;
    }

    if (keyEquals(patch, "chat_relay") || keyEquals(patch, "chat_ack_broadcast") ||
        keyEquals(patch, "chat_ack_squad") || keyEquals(patch, "chat_hop_default") ||
        keyEquals(patch, "chat_retries") || keyEquals(patch, "chat_max_channels"))
    {
        const ui::UiActionResult result = updateConfig(
            app::AppConfigChangeSet::chatUi(),
            [&](app::AppConfig& config)
            {
                bool value = false;
                if (keyEquals(patch, "chat_relay") && adjustedBool(patch, config.chat_policy.enable_relay, value))
                    config.chat_policy.enable_relay = value;
                else if (keyEquals(patch, "chat_ack_broadcast") && adjustedBool(patch, config.chat_policy.ack_for_broadcast, value))
                    config.chat_policy.ack_for_broadcast = value;
                else if (keyEquals(patch, "chat_ack_squad") && adjustedBool(patch, config.chat_policy.ack_for_squad, value))
                    config.chat_policy.ack_for_squad = value;
                else if (keyEquals(patch, "chat_hop_default"))
                    config.chat_policy.hop_limit_default = static_cast<uint8_t>(cycleInt(config.chat_policy.hop_limit_default, 1, 3, direction));
                else if (keyEquals(patch, "chat_retries"))
                    config.chat_policy.max_tx_retries = static_cast<uint8_t>(cycleInt(config.chat_policy.max_tx_retries, 0, 5, direction));
                else if (keyEquals(patch, "chat_max_channels"))
                    config.chat_policy.max_channels = static_cast<uint8_t>(cycleInt(config.chat_policy.max_channels, 1, 3, direction));
            });
        if (result.ok)
        {
            app::appFacade().applyChatDefaults();
        }
        return result;
    }

    if (keyEquals(patch, "net_duty_cycle") || keyEquals(patch, "net_channel_util") ||
        keyEquals(patch, "privacy_encrypt"))
    {
        const bool privacy = keyEquals(patch, "privacy_encrypt");
        const ui::UiActionResult result = updateConfig(
            privacy ? app::AppConfigChangeSet::privacy() : app::AppConfigChangeSet::network(),
            [&](app::AppConfig& config)
            {
                if (keyEquals(patch, "net_duty_cycle"))
                {
                    bool value = false;
                    if (adjustedBool(patch, config.net_duty_cycle, value))
                    {
                        config.net_duty_cycle = value;
                    }
                }
                else if (keyEquals(patch, "net_channel_util"))
                {
                    constexpr int values[] = {0, 25, 50, 75, 100};
                    config.net_channel_util = static_cast<uint8_t>(cycleChoice(config.net_channel_util, values, direction));
                }
                else
                {
                    config.privacy_encrypt_mode = static_cast<uint8_t>(cycleInt(config.privacy_encrypt_mode, 0, 2, direction));
                }
            });
        if (result.ok)
        {
            if (privacy)
                app::appFacade().applyPrivacyConfig();
            else
                app::appFacade().applyNetworkLimits();
        }
        return result;
    }

    if (keyEquals(patch, "mqtt_enabled") || keyEquals(patch, "mqtt_uplink") ||
        keyEquals(patch, "mqtt_downlink") || keyEquals(patch, "mqtt_host") ||
        keyEquals(patch, "mqtt_port") || keyEquals(patch, "mqtt_root") ||
        keyEquals(patch, "mqtt_user") || keyEquals(patch, "mqtt_password"))
    {
        const bool meshcore = current.mesh_protocol == chat::MeshProtocol::MeshCore;
        if ((keyEquals(patch, "mqtt_host") || keyEquals(patch, "mqtt_root") ||
             keyEquals(patch, "mqtt_user") || keyEquals(patch, "mqtt_password")) &&
            !patchTextIsUsable(patch))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        const ui::UiActionResult result = updateConfig(
            app::AppConfigChangeSet::mesh(),
            [&](app::AppConfig& config)
            {
                if (meshcore)
                {
                    auto& mqtt = config.meshcore_config;
                    bool value = false;
                    if (keyEquals(patch, "mqtt_enabled") && adjustedBool(patch, mqtt.meshcore_mqtt_enabled, value)) mqtt.meshcore_mqtt_enabled = value;
                    else if (keyEquals(patch, "mqtt_uplink") && adjustedBool(patch, mqtt.meshcore_mqtt_uplink_enabled, value)) mqtt.meshcore_mqtt_uplink_enabled = value;
                    else if (keyEquals(patch, "mqtt_downlink") && adjustedBool(patch, mqtt.meshcore_mqtt_downlink_enabled, value)) mqtt.meshcore_mqtt_downlink_enabled = value;
                    else if (keyEquals(patch, "mqtt_host")) std::snprintf(mqtt.meshcore_mqtt_host, sizeof(mqtt.meshcore_mqtt_host), "%s", patch.value.c_str());
                    else if (keyEquals(patch, "mqtt_port")) mqtt.meshcore_mqtt_port = static_cast<uint16_t>(cycleChoice(static_cast<int>(mqtt.meshcore_mqtt_port), kMqttPortChoices, direction));
                    else if (keyEquals(patch, "mqtt_root")) std::snprintf(mqtt.meshcore_mqtt_root, sizeof(mqtt.meshcore_mqtt_root), "%s", patch.value.c_str());
                    else if (keyEquals(patch, "mqtt_user")) std::snprintf(mqtt.meshcore_mqtt_username, sizeof(mqtt.meshcore_mqtt_username), "%s", patch.value.c_str());
                    else if (keyEquals(patch, "mqtt_password")) std::snprintf(mqtt.meshcore_mqtt_password, sizeof(mqtt.meshcore_mqtt_password), "%s", patch.value.c_str());
                }
                else
                {
                    bool value = false;
                    if (keyEquals(patch, "mqtt_enabled") && adjustedBool(patch, config.meshtastic_mqtt_enabled, value)) config.meshtastic_mqtt_enabled = value;
                    else if (keyEquals(patch, "mqtt_uplink") && adjustedBool(patch, config.meshtastic_mqtt_uplink_enabled, value)) config.meshtastic_mqtt_uplink_enabled = value;
                    else if (keyEquals(patch, "mqtt_downlink") && adjustedBool(patch, config.meshtastic_mqtt_downlink_enabled, value)) config.meshtastic_mqtt_downlink_enabled = value;
                    else if (keyEquals(patch, "mqtt_host")) std::snprintf(config.meshtastic_mqtt_host, sizeof(config.meshtastic_mqtt_host), "%s", patch.value.c_str());
                    else if (keyEquals(patch, "mqtt_port")) config.meshtastic_mqtt_port = static_cast<uint16_t>(cycleChoice(static_cast<int>(config.meshtastic_mqtt_port), kMqttPortChoices, direction));
                    else if (keyEquals(patch, "mqtt_root")) std::snprintf(config.meshtastic_mqtt_root, sizeof(config.meshtastic_mqtt_root), "%s", patch.value.c_str());
                    else if (keyEquals(patch, "mqtt_user")) std::snprintf(config.meshtastic_mqtt_username, sizeof(config.meshtastic_mqtt_username), "%s", patch.value.c_str());
                    else if (keyEquals(patch, "mqtt_password")) std::snprintf(config.meshtastic_mqtt_password, sizeof(config.meshtastic_mqtt_password), "%s", patch.value.c_str());
                }
            });
        if (result.ok)
        {
            app::appFacade().applyMeshConfig();
        }
        return result;
    }

    if (keyEquals(patch, "screen_brightness"))
    {
        if (direction == Direction::None)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        const uint8_t maximum = platform::ui::device::screen_brightness_max();
        if (maximum == 0)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
        }
        const int current_level = platform::ui::settings_store::get_int(
            kSettingsNamespace, "screen_brightness", platform::ui::device::screen_brightness());
        const int current_percent = (current_level * 100 + (maximum / 2)) / maximum;
        const int current_step = current_percent <= 10 ? 1 : (current_percent + 5) / 10;
        const int next_step = cycleInt(current_step, 1, 10, direction);
        const uint8_t level = platform::ui::screen_brightness_steps::levelForStep(
            static_cast<size_t>(next_step - 1), maximum);
        platform::ui::settings_store::put_int(kSettingsNamespace, "screen_brightness", level);
        platform::ui::device::set_screen_brightness(level);
        return ui::UiActionResult::success();
    }

    if (keyEquals(patch, "speaker_volume"))
    {
        if (direction == Direction::None)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        constexpr int values[] = {0, 30, 45, 60, 75, 90, 100};
        const int volume = cycleChoice(
            platform::ui::settings_store::get_int(kSettingsNamespace, "speaker_volume", 45), values, direction);
        platform::ui::settings_store::put_int(kSettingsNamespace, "speaker_volume", volume);
        platform::ui::device::set_message_tone_volume(static_cast<uint8_t>(volume));
        return ui::UiActionResult::success();
    }

    if (keyEquals(patch, "vibration_enabled"))
    {
        bool enabled = false;
        if (!adjustedBool(patch,
                          platform::ui::settings_store::get_bool(kSettingsNamespace, "vibration_enabled", true),
                          enabled))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        platform::ui::settings_store::put_bool(kSettingsNamespace, "vibration_enabled", enabled);
        return ui::UiActionResult::success();
    }

    if (keyEquals(patch, "timezone_profile"))
    {
        if (direction == Direction::None)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        size_t count = 0;
        const auto* profiles = platform::ui::time::timezone_profiles(&count);
        if (!profiles || count == 0)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
        }
        size_t index = 0;
        for (; index < count; ++index)
        {
            if (profiles[index].id == platform::ui::time::timezone_profile_id())
            {
                break;
            }
        }
        if (index >= count)
        {
            index = 0;
        }
        index = direction == Direction::Next ? (index + 1U) % count : (index == 0 ? count - 1U : index - 1U);
        platform::ui::time::set_timezone_profile_id(profiles[index].id);
        return ui::UiActionResult::success();
    }

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    if (keyEquals(patch, "cellular_enabled") || keyEquals(patch, "cellular.enabled"))
    {
        bool enabled = false;
        if (!adjustedBool(patch, ::platform::ui::a7682e::status().enabled, enabled))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        return ::platform::ui::a7682e::set_enabled(enabled)
                   ? ui::UiActionResult::success()
                   : ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }
#endif

    return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
}

RuntimeSettingsSource& runtime_settings_source()
{
    static RuntimeSettingsSource source;
    return source;
}

RuntimeSettingsActionSink& runtime_settings_action_sink()
{
    static RuntimeSettingsActionSink sink;
    return sink;
}

} // namespace ui::presentation_sources
