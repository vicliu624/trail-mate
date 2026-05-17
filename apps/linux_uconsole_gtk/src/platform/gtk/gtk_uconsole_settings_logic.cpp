#include "platform/gtk/gtk_uconsole_mqtt_settings.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshtastic/mt_region.h"

namespace trailmate::uconsole::gtk
{

constexpr std::array<::chat::MeshProtocol, 4> kSettingsProtocols{{
    ::chat::MeshProtocol::Meshtastic,
    ::chat::MeshProtocol::MeshCore,
    ::chat::MeshProtocol::RNode,
    ::chat::MeshProtocol::LXMF,
}};

constexpr std::array<meshtastic_Config_LoRaConfig_ModemPreset, 10>
    kMeshtasticPresetOptions{{
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE,
        meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO,
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,
    }};

int protocolIndex(::chat::MeshProtocol protocol)
{
    for (std::size_t index = 0; index < kSettingsProtocols.size(); ++index)
    {
        if (kSettingsProtocols[index] == protocol)
        {
            return static_cast<int>(index);
        }
    }
    return 0;
}

std::vector<const char*> meshtasticRegionLabels()
{
    std::size_t count = 0;
    const auto* regions = ::chat::meshtastic::getRegionTable(&count);
    std::vector<const char*> labels{};
    labels.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        labels.push_back(regions[index].label);
    }
    return labels;
}

int meshtasticRegionIndex(std::uint8_t region_code)
{
    std::size_t count = 0;
    const auto* regions = ::chat::meshtastic::getRegionTable(&count);
    for (std::size_t index = 0; index < count; ++index)
    {
        if (static_cast<std::uint8_t>(regions[index].code) == region_code)
        {
            return static_cast<int>(index);
        }
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        if (regions[index].code ==
            meshtastic_Config_LoRaConfig_RegionCode_CN)
        {
            return static_cast<int>(index);
        }
    }
    return 0;
}

std::uint8_t meshtasticRegionFromIndex(int index)
{
    std::size_t count = 0;
    const auto* regions = ::chat::meshtastic::getRegionTable(&count);
    if (index >= 0 && static_cast<std::size_t>(index) < count)
    {
        return static_cast<std::uint8_t>(regions[index].code);
    }
    return ::app::AppConfig::kDefaultRegionCode;
}

std::vector<const char*> meshtasticPresetLabels()
{
    std::vector<const char*> labels{};
    labels.reserve(kMeshtasticPresetOptions.size());
    for (const auto preset : kMeshtasticPresetOptions)
    {
        labels.push_back(::chat::meshtastic::presetDisplayName(preset));
    }
    return labels;
}

int meshtasticPresetIndex(std::uint8_t preset_value)
{
    for (std::size_t index = 0; index < kMeshtasticPresetOptions.size();
         ++index)
    {
        if (static_cast<std::uint8_t>(kMeshtasticPresetOptions[index]) ==
            preset_value)
        {
            return static_cast<int>(index);
        }
    }
    return 0;
}

std::uint8_t meshtasticPresetFromIndex(int index)
{
    if (index >= 0 &&
        static_cast<std::size_t>(index) < kMeshtasticPresetOptions.size())
    {
        return static_cast<std::uint8_t>(
            kMeshtasticPresetOptions[static_cast<std::size_t>(index)]);
    }
    return static_cast<std::uint8_t>(
        meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
}

::chat::MeshProtocol protocolFromIndex(int index)
{
    if (index < 0 ||
        static_cast<std::size_t>(index) >= kSettingsProtocols.size())
    {
        return ::chat::MeshProtocol::Meshtastic;
    }
    return kSettingsProtocols[static_cast<std::size_t>(index)];
}

void copyBounded(char* out, std::size_t out_len, const char* text)
{
    if (out == nullptr || out_len == 0U)
    {
        return;
    }
    if (text == nullptr)
    {
        out[0] = '\0';
        return;
    }
    std::snprintf(out, out_len, "%s", text);
}

::chat::MeshConfig& meshConfigForProtocol(::app::AppConfig& config,
                                          ::chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case ::chat::MeshProtocol::MeshCore:
        return config.meshcore_config;
    case ::chat::MeshProtocol::RNode:
    case ::chat::MeshProtocol::LXMF:
        return config.rnode_config;
    case ::chat::MeshProtocol::Meshtastic:
    default:
        return config.meshtastic_config;
    }
}

const ::chat::MeshConfig& meshConfigForProtocol(
    const ::app::AppConfig& config,
    ::chat::MeshProtocol protocol)
{
    switch (protocol)
    {
    case ::chat::MeshProtocol::MeshCore:
        return config.meshcore_config;
    case ::chat::MeshProtocol::RNode:
    case ::chat::MeshProtocol::LXMF:
        return config.rnode_config;
    case ::chat::MeshProtocol::Meshtastic:
    default:
        return config.meshtastic_config;
    }
}

float displayFrequencyMhz(const ::chat::MeshConfig& mesh)
{
    if (mesh.override_frequency_mhz > 0.0F)
    {
        return mesh.override_frequency_mhz;
    }
    if (mesh.meshcore_freq_mhz > 0.0F && mesh.meshcore_freq_mhz < 1000.0F)
    {
        return mesh.meshcore_freq_mhz;
    }
    return 433.175F;
}

float displayBandwidthKHz(const ::chat::MeshConfig& mesh,
                          ::chat::MeshProtocol protocol)
{
    if (protocol == ::chat::MeshProtocol::MeshCore &&
        mesh.meshcore_bw_khz > 0.0F)
    {
        return mesh.meshcore_bw_khz;
    }
    return mesh.bandwidth_khz;
}

std::uint8_t displaySpreadFactor(const ::chat::MeshConfig& mesh,
                                 ::chat::MeshProtocol protocol)
{
    if (protocol == ::chat::MeshProtocol::MeshCore &&
        mesh.meshcore_sf >= 5U && mesh.meshcore_sf <= 12U)
    {
        return mesh.meshcore_sf;
    }
    return mesh.spread_factor;
}

std::uint8_t displayCodingRate(const ::chat::MeshConfig& mesh,
                               ::chat::MeshProtocol protocol)
{
    if (protocol == ::chat::MeshProtocol::MeshCore &&
        mesh.meshcore_cr >= 5U && mesh.meshcore_cr <= 8U)
    {
        return mesh.meshcore_cr;
    }
    return mesh.coding_rate;
}

int meshtasticRegionTxPowerLimitDbm(std::uint8_t region_code)
{
    const auto* region = ::chat::meshtastic::findRegion(
        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(region_code));
    int limit = static_cast<int>(::app::AppConfig::kTxPowerMaxDbm);
    if (region != nullptr && region->power_limit_dbm > 0U)
    {
        limit = std::min(limit, static_cast<int>(region->power_limit_dbm));
    }
    return std::clamp(limit,
                      static_cast<int>(::app::AppConfig::kTxPowerMinDbm),
                      static_cast<int>(::app::AppConfig::kTxPowerMaxDbm));
}

int displayTxPowerDbm(const ::chat::MeshConfig& mesh)
{
    return std::clamp(static_cast<int>(mesh.tx_power),
                      static_cast<int>(::app::AppConfig::kTxPowerMinDbm),
                      meshtasticRegionTxPowerLimitDbm(mesh.region));
}

void setMeshtasticTxPowerControl(GtkWidget* control,
                                 const ::chat::MeshConfig& mesh)
{
    if (control == nullptr)
    {
        return;
    }
    const int limit = meshtasticRegionTxPowerLimitDbm(mesh.region);
    GtkAdjustment* adjustment =
        gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(control));
    gtk_adjustment_set_lower(
        adjustment, static_cast<double>(::app::AppConfig::kTxPowerMinDbm));
    gtk_adjustment_set_upper(adjustment, static_cast<double>(limit));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(control),
                              static_cast<double>(displayTxPowerDbm(mesh)));

    const std::string tip =
        "Selected Meshtastic region limit: " + std::to_string(limit) + " dBm";
    gtk_widget_set_tooltip_text(control, tip.c_str());
}
void showSettingsNotice(GtkUConsoleAppState& state, const char* text)
{
    state.settings_notice = text ? text : "";
    state.settings_notice_ticks = 8;
    setLabel(state.settings_status, state.settings_notice);
}
::chat::MeshProtocol selectedSettingsProtocol(const GtkUConsoleAppState& state)
{
    if (state.settings_protocol == nullptr)
    {
        return state.services.config().mesh_protocol;
    }
    return protocolFromIndex(
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_protocol)));
}

void setSettingsStackPageVisible(GtkUConsoleAppState& state,
                                 GtkWidget* child,
                                 bool visible)
{
    if (state.settings_group_stack == nullptr || child == nullptr)
    {
        return;
    }

    GtkStackPage* page =
        gtk_stack_get_page(GTK_STACK(state.settings_group_stack), child);
    if (page != nullptr)
    {
        gtk_stack_page_set_visible(page, visible ? TRUE : FALSE);
    }
    else
    {
        gtk_widget_set_visible(child, visible ? TRUE : FALSE);
    }
}

void updateSettingsProtocolVisibility(GtkUConsoleAppState& state)
{
    const auto protocol = selectedSettingsProtocol(state);
    const bool meshtastic = protocol == ::chat::MeshProtocol::Meshtastic;
    const bool meshcore = protocol == ::chat::MeshProtocol::MeshCore;
    const bool raw_lora = protocol == ::chat::MeshProtocol::RNode ||
                          protocol == ::chat::MeshProtocol::LXMF;

    setSettingsStackPageVisible(state,
                                state.settings_meshtastic_page,
                                meshtastic);
    setSettingsStackPageVisible(state, state.settings_meshcore_page, meshcore);
    setSettingsStackPageVisible(state, state.settings_link_page, raw_lora);

    if (state.settings_group_stack == nullptr)
    {
        return;
    }

    const char* visible_name = gtk_stack_get_visible_child_name(
        GTK_STACK(state.settings_group_stack));
    const std::string current = visible_name ? visible_name : "";
    if ((current == "meshtastic" && !meshtastic) ||
        (current == "meshcore" && !meshcore) ||
        (current == "link" && !raw_lora))
    {
        gtk_stack_set_visible_child_name(
            GTK_STACK(state.settings_group_stack),
            meshtastic ? "meshtastic" : (meshcore ? "meshcore" : "link"));
    }
}
void populateSettingsControls(GtkUConsoleAppState& state)
{
    const auto& config = state.services.config();
    const auto mesh = config.activeMeshConfig();
    const auto protocol = config.mesh_protocol;
    const auto mqtt = loadMqttSettings();

    if (state.settings_node_name != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_node_name),
                              config.node_name);
    }
    if (state.settings_short_name != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_short_name),
                              config.short_name);
    }
    if (state.settings_protocol != nullptr)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.settings_protocol),
                                 protocolIndex(config.mesh_protocol));
    }
    if (state.settings_lora_region != nullptr)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.settings_lora_region),
                                 meshtasticRegionIndex(mesh.region));
    }
    if (state.settings_lora_use_preset != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_lora_use_preset),
                              mesh.use_preset);
    }
    if (state.settings_lora_modem_preset != nullptr)
    {
        gtk_combo_box_set_active(
            GTK_COMBO_BOX(state.settings_lora_modem_preset),
            meshtasticPresetIndex(mesh.modem_preset));
    }
    if (state.settings_lora_freq != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_lora_freq),
                                  displayFrequencyMhz(mesh));
    }
    if (state.settings_lora_bw != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_lora_bw),
                                  displayBandwidthKHz(mesh, protocol));
    }
    if (state.settings_lora_sf != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_lora_sf),
                                  displaySpreadFactor(mesh, protocol));
    }
    if (state.settings_lora_cr != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_lora_cr),
                                  displayCodingRate(mesh, protocol));
    }
    if (state.settings_lora_tx != nullptr)
    {
        setMeshtasticTxPowerControl(state.settings_lora_tx, mesh);
    }
    if (state.settings_hop_limit != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_hop_limit),
                                  mesh.hop_limit);
    }
    if (state.settings_tx_enabled != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_tx_enabled),
                              mesh.tx_enabled);
    }
    if (state.settings_lora_channel_num != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_lora_channel_num),
            mesh.channel_num);
    }
    if (state.settings_lora_freq_offset != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_lora_freq_offset),
            mesh.frequency_offset_mhz);
    }
    if (state.settings_lora_override_duty != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_lora_override_duty),
                              mesh.override_duty_cycle);
    }
    if (state.settings_lora_ignore_mqtt != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_lora_ignore_mqtt),
                              mesh.ignore_mqtt);
    }
    if (state.settings_lora_ok_to_mqtt != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_lora_ok_to_mqtt),
                              mesh.config_ok_to_mqtt);
    }
    if (state.settings_meshcore_region_preset != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_region_preset),
            mesh.meshcore_region_preset);
    }
    if (state.settings_meshcore_channel_slot != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_channel_slot),
            mesh.meshcore_channel_slot);
    }
    if (state.settings_meshcore_channel_name != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_meshcore_channel_name),
                              mesh.meshcore_channel_name);
    }
    if (state.settings_meshcore_client_repeat != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_meshcore_client_repeat),
                              mesh.meshcore_client_repeat);
    }
    if (state.settings_meshcore_rx_delay != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_rx_delay),
            mesh.meshcore_rx_delay_base);
    }
    if (state.settings_meshcore_airtime != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_airtime),
            mesh.meshcore_airtime_factor);
    }
    if (state.settings_meshcore_flood_max != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_flood_max),
            mesh.meshcore_flood_max);
    }
    if (state.settings_meshcore_multi_acks != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_meshcore_multi_acks),
                              mesh.meshcore_multi_acks);
    }
    if (state.settings_primary_enabled != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_primary_enabled),
                              config.primary_enabled);
    }
    if (state.settings_secondary_enabled != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_secondary_enabled),
                              config.secondary_enabled);
    }
    if (state.settings_primary_uplink != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_primary_uplink),
                              config.primary_uplink_enabled);
    }
    if (state.settings_primary_downlink != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_primary_downlink),
                              config.primary_downlink_enabled);
    }
    if (state.settings_secondary_uplink != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_secondary_uplink),
                              config.secondary_uplink_enabled);
    }
    if (state.settings_secondary_downlink != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_secondary_downlink),
                              config.secondary_downlink_enabled);
    }
    if (state.settings_chat_channel != nullptr)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.settings_chat_channel),
                                 std::clamp<int>(config.chat_channel, 0, 1));
    }
    if (state.settings_relay_enabled != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_relay_enabled),
                              config.chat_policy.enable_relay);
    }
    if (state.settings_ack_broadcast != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_ack_broadcast),
                              config.chat_policy.ack_for_broadcast);
    }
    if (state.settings_ack_squad != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_ack_squad),
                              config.chat_policy.ack_for_squad);
    }
    if (state.settings_tx_retries != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_tx_retries),
                                  config.chat_policy.max_tx_retries);
    }
    if (state.settings_max_channels != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_max_channels),
                                  config.chat_policy.max_channels);
    }
    if (state.settings_gps_enabled != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_gps_enabled),
                              config.gps_enabled);
    }
    if (state.settings_gps_interval != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_gps_interval),
                                  config.gps_interval_ms);
    }
    if (state.settings_gps_mode != nullptr)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.settings_gps_mode),
                                 std::clamp<int>(config.gps_mode, 0, 2));
    }
    if (state.settings_gps_strategy != nullptr)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.settings_gps_strategy),
                                 std::clamp<int>(config.gps_strategy, 0, 2));
    }
    if (state.settings_external_nmea_hz != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_external_nmea_hz),
            config.external_nmea_output_hz);
    }
    if (state.settings_external_nmea_mask != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_external_nmea_mask),
            config.external_nmea_sentence_mask);
    }
    if (state.settings_map_source != nullptr)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.settings_map_source),
                                 std::clamp<int>(config.map_source, 0, 2));
    }
    if (state.settings_map_zoom != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_map_zoom),
                                  state.map_model.snapshot().zoom);
    }
    if (state.settings_map_contour != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_map_contour),
                              config.map_contour_enabled);
    }
    if (state.settings_map_contour_ultra_fine != nullptr)
    {
        gtk_switch_set_active(
            GTK_SWITCH(state.settings_map_contour_ultra_fine),
            state.map_model.contourUltraFineEnabled());
    }
    if (state.settings_map_earthdata_token != nullptr)
    {
        const auto token = state.map_model.earthdataToken();
        gtk_editable_set_text(GTK_EDITABLE(state.settings_map_earthdata_token),
                              token.c_str());
    }
    if (state.settings_map_track != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_map_track),
                              config.map_track_enabled);
    }
    if (state.settings_map_mqtt_nodes != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_map_mqtt_nodes),
                              state.map_model.snapshot().show_mqtt_nodes);
    }
    if (state.settings_map_track_interval != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_map_track_interval),
            config.map_track_interval);
    }
    if (state.settings_map_track_format != nullptr)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.settings_map_track_format),
                                 std::clamp<int>(config.map_track_format, 0, 2));
    }
    if (state.settings_mqtt_enabled != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_mqtt_enabled),
                              mqtt.enabled);
    }
    if (state.settings_mqtt_name != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_name),
                              mqtt.name.c_str());
    }
    if (state.settings_mqtt_host != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_host),
                              mqtt.host.c_str());
    }
    if (state.settings_mqtt_port != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_mqtt_port),
                                  mqtt.port);
    }
    if (state.settings_mqtt_username != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_username),
                              mqtt.username.c_str());
    }
    if (state.settings_mqtt_password != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_password),
                              mqtt.password.c_str());
    }
    if (state.settings_mqtt_topic != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_topic),
                              mqtt.topic.c_str());
    }
    if (state.settings_mqtt_tls != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_mqtt_tls), mqtt.tls);
    }
    if (state.settings_mqtt_client_id != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_client_id),
                              mqtt.client_id.c_str());
    }
    if (state.settings_mqtt_clean_session != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_mqtt_clean_session),
                              mqtt.clean_session);
    }
    if (state.settings_mqtt_qos != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_mqtt_qos),
                                  mqtt.qos);
    }
    if (state.settings_net_duty_cycle != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_net_duty_cycle),
                              config.net_duty_cycle);
    }
    if (state.settings_net_channel_util != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_net_channel_util),
            config.net_channel_util);
    }
    if (state.settings_privacy_encrypt_mode != nullptr)
    {
        gtk_combo_box_set_active(
            GTK_COMBO_BOX(state.settings_privacy_encrypt_mode),
            std::clamp<int>(config.privacy_encrypt_mode, 0, 2));
    }
    updateSettingsProtocolVisibility(state);
    showSettingsNotice(state, "Settings loaded.");
}

void onSettingsProtocolChanged(GtkComboBox*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const int index = gtk_combo_box_get_active(
        GTK_COMBO_BOX(state.settings_protocol));
    const auto protocol = protocolFromIndex(index);
    const auto& mesh = meshConfigForProtocol(state.services.config(), protocol);
    if (state.settings_lora_region != nullptr)
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.settings_lora_region),
                                 meshtasticRegionIndex(mesh.region));
    }
    if (state.settings_lora_use_preset != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_lora_use_preset),
                              mesh.use_preset);
    }
    if (state.settings_lora_modem_preset != nullptr)
    {
        gtk_combo_box_set_active(
            GTK_COMBO_BOX(state.settings_lora_modem_preset),
            meshtasticPresetIndex(mesh.modem_preset));
    }
    if (state.settings_lora_freq != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_lora_freq),
                                  displayFrequencyMhz(mesh));
    }
    if (state.settings_lora_bw != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_lora_bw),
                                  displayBandwidthKHz(mesh, protocol));
    }
    if (state.settings_lora_sf != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_lora_sf),
                                  displaySpreadFactor(mesh, protocol));
    }
    if (state.settings_lora_cr != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_lora_cr),
                                  displayCodingRate(mesh, protocol));
    }
    if (state.settings_lora_tx != nullptr)
    {
        setMeshtasticTxPowerControl(state.settings_lora_tx, mesh);
    }
    if (state.settings_hop_limit != nullptr)
    {
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(state.settings_hop_limit),
                                  mesh.hop_limit);
    }
    if (state.settings_tx_enabled != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_tx_enabled),
                              mesh.tx_enabled);
    }
    if (state.settings_lora_channel_num != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_lora_channel_num),
            mesh.channel_num);
    }
    if (state.settings_lora_freq_offset != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_lora_freq_offset),
            mesh.frequency_offset_mhz);
    }
    if (state.settings_lora_override_duty != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_lora_override_duty),
                              mesh.override_duty_cycle);
    }
    if (state.settings_lora_ignore_mqtt != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_lora_ignore_mqtt),
                              mesh.ignore_mqtt);
    }
    if (state.settings_lora_ok_to_mqtt != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_lora_ok_to_mqtt),
                              mesh.config_ok_to_mqtt);
    }
    if (state.settings_meshcore_region_preset != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_region_preset),
            mesh.meshcore_region_preset);
    }
    if (state.settings_meshcore_channel_slot != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_channel_slot),
            mesh.meshcore_channel_slot);
    }
    if (state.settings_meshcore_channel_name != nullptr)
    {
        gtk_editable_set_text(GTK_EDITABLE(state.settings_meshcore_channel_name),
                              mesh.meshcore_channel_name);
    }
    if (state.settings_meshcore_client_repeat != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_meshcore_client_repeat),
                              mesh.meshcore_client_repeat);
    }
    if (state.settings_meshcore_rx_delay != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_rx_delay),
            mesh.meshcore_rx_delay_base);
    }
    if (state.settings_meshcore_airtime != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_airtime),
            mesh.meshcore_airtime_factor);
    }
    if (state.settings_meshcore_flood_max != nullptr)
    {
        gtk_spin_button_set_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_flood_max),
            mesh.meshcore_flood_max);
    }
    if (state.settings_meshcore_multi_acks != nullptr)
    {
        gtk_switch_set_active(GTK_SWITCH(state.settings_meshcore_multi_acks),
                              mesh.meshcore_multi_acks);
    }
    updateSettingsProtocolVisibility(state);
}

void onSettingsMeshtasticRegionChanged(GtkComboBox*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.settings_lora_region == nullptr ||
        state.settings_lora_tx == nullptr)
    {
        return;
    }

    auto mesh = state.services.config().meshtastic_config;
    mesh.region = meshtasticRegionFromIndex(
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_lora_region)));
    mesh.tx_power = static_cast<std::int8_t>(
        gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(state.settings_lora_tx)));
    setMeshtasticTxPowerControl(state.settings_lora_tx, mesh);
}

void onSettingsApplyClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    auto& config = state.services.config();

    copyBounded(config.node_name,
                sizeof(config.node_name),
                gtk_editable_get_text(GTK_EDITABLE(state.settings_node_name)));
    copyBounded(config.short_name,
                sizeof(config.short_name),
                gtk_editable_get_text(GTK_EDITABLE(state.settings_short_name)));

    const auto protocol = protocolFromIndex(
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_protocol)));
    config.mesh_protocol = protocol;
    auto& mesh = meshConfigForProtocol(config, protocol);
    const auto selected_region = meshtasticRegionFromIndex(
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_lora_region)));
    mesh.region = selected_region;
    mesh.use_preset =
        gtk_switch_get_active(GTK_SWITCH(state.settings_lora_use_preset));
    mesh.modem_preset = meshtasticPresetFromIndex(gtk_combo_box_get_active(
        GTK_COMBO_BOX(state.settings_lora_modem_preset)));
    mesh.override_frequency_mhz = static_cast<float>(
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(state.settings_lora_freq)));
    mesh.meshcore_freq_mhz = mesh.override_frequency_mhz;
    const float bandwidth_khz = static_cast<float>(
        gtk_spin_button_get_value(GTK_SPIN_BUTTON(state.settings_lora_bw)));
    const auto spread_factor = static_cast<std::uint8_t>(
        gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(state.settings_lora_sf)));
    const auto coding_rate = static_cast<std::uint8_t>(
        gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(state.settings_lora_cr)));
    mesh.bandwidth_khz = bandwidth_khz;
    mesh.spread_factor = spread_factor;
    mesh.coding_rate = coding_rate;
    mesh.meshcore_bw_khz = bandwidth_khz;
    mesh.meshcore_sf = spread_factor;
    mesh.meshcore_cr = coding_rate;
    mesh.tx_power = static_cast<std::int8_t>(std::clamp(
        gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(state.settings_lora_tx)),
        static_cast<int>(::app::AppConfig::kTxPowerMinDbm),
        meshtasticRegionTxPowerLimitDbm(mesh.region)));
    mesh.hop_limit = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_hop_limit)),
                   1,
                   7));
    mesh.tx_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_tx_enabled));
    mesh.channel_num = static_cast<std::uint16_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_lora_channel_num)),
                   0,
                   65535));
    mesh.frequency_offset_mhz = static_cast<float>(
        gtk_spin_button_get_value(
            GTK_SPIN_BUTTON(state.settings_lora_freq_offset)));
    mesh.override_duty_cycle =
        gtk_switch_get_active(GTK_SWITCH(state.settings_lora_override_duty));
    mesh.ignore_mqtt =
        gtk_switch_get_active(GTK_SWITCH(state.settings_lora_ignore_mqtt));
    mesh.config_ok_to_mqtt =
        gtk_switch_get_active(GTK_SWITCH(state.settings_lora_ok_to_mqtt));
    mesh.meshcore_region_preset = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_meshcore_region_preset)),
                   0,
                   255));
    mesh.meshcore_channel_slot = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_meshcore_channel_slot)),
                   0,
                   255));
    copyBounded(mesh.meshcore_channel_name,
                sizeof(mesh.meshcore_channel_name),
                gtk_editable_get_text(
                    GTK_EDITABLE(state.settings_meshcore_channel_name)));
    mesh.meshcore_client_repeat =
        gtk_switch_get_active(GTK_SWITCH(state.settings_meshcore_client_repeat));
    mesh.meshcore_rx_delay_base = static_cast<float>(
        gtk_spin_button_get_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_rx_delay)));
    mesh.meshcore_airtime_factor = static_cast<float>(
        gtk_spin_button_get_value(
            GTK_SPIN_BUTTON(state.settings_meshcore_airtime)));
    mesh.meshcore_flood_max = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_meshcore_flood_max)),
                   0,
                   255));
    mesh.meshcore_multi_acks =
        gtk_switch_get_active(GTK_SWITCH(state.settings_meshcore_multi_acks));

    config.primary_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_primary_enabled));
    config.secondary_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_secondary_enabled));
    config.primary_uplink_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_primary_uplink));
    config.primary_downlink_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_primary_downlink));
    config.secondary_uplink_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_secondary_uplink));
    config.secondary_downlink_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_secondary_downlink));

    config.chat_channel = static_cast<std::uint8_t>(std::clamp(
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_chat_channel)),
        0,
        1));
    config.chat_policy.enable_relay =
        gtk_switch_get_active(GTK_SWITCH(state.settings_relay_enabled));
    config.chat_policy.ack_for_broadcast =
        gtk_switch_get_active(GTK_SWITCH(state.settings_ack_broadcast));
    config.chat_policy.ack_for_squad =
        gtk_switch_get_active(GTK_SWITCH(state.settings_ack_squad));
    config.chat_policy.max_tx_retries = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_tx_retries)),
                   0,
                   5));
    config.chat_policy.max_channels = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_max_channels)),
                   1,
                   3));
    mesh.enable_relay = config.chat_policy.enable_relay;

    config.gps_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_gps_enabled));
    config.gps_interval_ms = static_cast<std::uint32_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_gps_interval)),
                   1000,
                   3600000));
    config.gps_mode = static_cast<std::uint8_t>(std::clamp(
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_gps_mode)),
        0,
        2));
    config.gps_strategy = static_cast<std::uint8_t>(std::clamp(
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_gps_strategy)),
        0,
        2));
    config.external_nmea_output_hz = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_external_nmea_hz)),
                   0,
                   10));
    config.external_nmea_sentence_mask = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_external_nmea_mask)),
                   0,
                   255));

    const int map_source =
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_map_source));
    state.map_model.setSource(
        ::platform::linux_runtime::sanitize_map_base_source(
            static_cast<std::uint8_t>(std::clamp(map_source, 0, 2))));
    state.map_model.setZoom(gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(state.settings_map_zoom)));
    state.map_model.setContourEnabled(
        gtk_switch_get_active(GTK_SWITCH(state.settings_map_contour)));
    state.map_model.setContourUltraFineEnabled(gtk_switch_get_active(
        GTK_SWITCH(state.settings_map_contour_ultra_fine)));
    state.map_model.setEarthdataToken(gtk_editable_get_text(
        GTK_EDITABLE(state.settings_map_earthdata_token)));
    config.map_track_enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_map_track));
    state.map_model.setShowMqttNodes(
        gtk_switch_get_active(GTK_SWITCH(state.settings_map_mqtt_nodes)));
    config.map_track_interval = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_map_track_interval)),
                   1,
                   99));
    config.map_track_format = static_cast<std::uint8_t>(std::clamp(
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.settings_map_track_format)),
        0,
        2));

    LinuxMqttUiSettings mqtt{};
    mqtt.enabled =
        gtk_switch_get_active(GTK_SWITCH(state.settings_mqtt_enabled));
    mqtt.name = gtk_editable_get_text(GTK_EDITABLE(state.settings_mqtt_name));
    mqtt.host = gtk_editable_get_text(GTK_EDITABLE(state.settings_mqtt_host));
    mqtt.port = std::clamp(gtk_spin_button_get_value_as_int(
                               GTK_SPIN_BUTTON(state.settings_mqtt_port)),
                           1,
                           65535);
    mqtt.username =
        gtk_editable_get_text(GTK_EDITABLE(state.settings_mqtt_username));
    mqtt.password =
        gtk_editable_get_text(GTK_EDITABLE(state.settings_mqtt_password));
    mqtt.topic = gtk_editable_get_text(GTK_EDITABLE(state.settings_mqtt_topic));
    mqtt.tls = gtk_switch_get_active(GTK_SWITCH(state.settings_mqtt_tls));
    mqtt.client_id =
        gtk_editable_get_text(GTK_EDITABLE(state.settings_mqtt_client_id));
    mqtt.clean_session =
        gtk_switch_get_active(GTK_SWITCH(state.settings_mqtt_clean_session));
    mqtt.qos = std::clamp(gtk_spin_button_get_value_as_int(
                              GTK_SPIN_BUTTON(state.settings_mqtt_qos)),
                          0,
                          2);
    saveMqttSettings(mqtt);
    ::platform::linux_runtime::PacketLogEntry mqtt_log{};
    mqtt_log.source = ::platform::linux_runtime::PacketLogSource::Mqtt;
    mqtt_log.direction =
        ::platform::linux_runtime::PacketLogDirection::System;
    mqtt_log.title = "MQTT settings saved";
    mqtt_log.summary = std::string(mqtt.enabled ? "Enabled" : "Disabled") +
                       " / " + mqtt.host + ":" +
                       std::to_string(mqtt.port) + " / " + mqtt.topic;
    mqtt_log.segments.push_back({
        .kind = ::platform::linux_runtime::PacketLogSegmentKind::Header,
        .label = "source",
        .text = mqtt.name,
    });
    mqtt_log.segments.push_back({
        .kind = ::platform::linux_runtime::PacketLogSegmentKind::Meta,
        .label = "transport",
        .text = std::string(mqtt.tls ? "tls" : "tcp") + " qos " +
                std::to_string(mqtt.qos),
    });
    ::platform::linux_runtime::append_packet_log(std::move(mqtt_log));

    config.net_duty_cycle =
        gtk_switch_get_active(GTK_SWITCH(state.settings_net_duty_cycle));
    config.net_channel_util = static_cast<std::uint8_t>(
        std::clamp(gtk_spin_button_get_value_as_int(
                       GTK_SPIN_BUTTON(state.settings_net_channel_util)),
                   0,
                   100));
    config.privacy_encrypt_mode = static_cast<std::uint8_t>(std::clamp(
        gtk_combo_box_get_active(
            GTK_COMBO_BOX(state.settings_privacy_encrypt_mode)),
        0,
        2));

    state.services.applyUserInfo();
    state.services.applyMeshConfig();
    state.services.applyPositionConfig();
    state.services.applyChatDefaults();
    state.services.applyNetworkLimits();
    state.services.applyPrivacyConfig();
    state.services.saveConfig();
    state.map_failed_tiles.clear();
    state.map_fetch_status.clear();
    showSettingsNotice(state, "Settings saved.");
    refreshUi(state);
}

void onSettingsReloadClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    populateSettingsControls(state);
}
static void refreshSettingsPage(GtkUConsoleAppState& state,
                                const UConsoleDashboardSnapshot& dashboard,
                                const MapWorkspaceSnapshot& map_snapshot)
{
    if (state.settings_status == nullptr)
    {
        return;
    }

    if (!state.settings_notice.empty() && state.settings_notice_ticks > 0)
    {
        setLabel(state.settings_status, state.settings_notice);
        --state.settings_notice_ticks;
        return;
    }
    state.settings_notice.clear();

    const std::string status =
        dashboard.self_node + " / " + dashboard.mesh_protocol + " / " +
        map_snapshot.source_label + " z" + std::to_string(map_snapshot.zoom);
    setLabel(state.settings_status, status);
}

static void showSettingsPage(GtkUConsoleAppState& state)
{
    populateSettingsControls(state);
}

void refreshSettingsLogic(GtkUConsoleAppState& state,
                          const GtkUConsoleRefreshSnapshot& snapshot)
{
    refreshSettingsPage(state, snapshot.dashboard, snapshot.map);
}

GtkUConsolePageLifecycle makeSettingsPageLifecycle()
{
    return {.name = "settings",
            .title = "Settings",
            .onLaunch = launchSettingsLayout,
            .onShow = showSettingsPage,
            .onHide = nullptr,
            .onRefresh = refreshSettingsLogic,
            .onDestroy = nullptr};
}

} // namespace trailmate::uconsole::gtk
