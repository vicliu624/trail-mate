#include "platform/gtk/gtk_uconsole_mqtt_settings.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <algorithm>
#include <vector>

#include "chat/infra/mesh_protocol_utils.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* makeSettingsSection(const char* title)
{
    GtkWidget* section = makePanel();
    gtk_widget_add_css_class(section, "settings-section");
    gtk_widget_set_vexpand(section, TRUE);
    gtk_box_append(GTK_BOX(section), makeLabel(title, "row-title"));
    return section;
}

GtkWidget* makeSettingsRow(const char* title,
                           const char* detail,
                           GtkWidget* control)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "settings-row");
    gtk_widget_set_hexpand(row, TRUE);

    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(text, TRUE);
    gtk_box_append(GTK_BOX(text), makeLabel(title, "row-title"));
    if (detail != nullptr && detail[0] != '\0')
    {
        gtk_box_append(GTK_BOX(text), makeLabel(detail, "row-meta", true));
    }
    gtk_box_append(GTK_BOX(row), text);

    if (control != nullptr)
    {
        if (GTK_IS_SWITCH(control))
        {
            gtk_widget_add_css_class(control, "settings-switch");
            gtk_widget_set_halign(control, GTK_ALIGN_END);
            gtk_widget_set_size_request(control, 48, -1);
        }
        else
        {
            gtk_widget_add_css_class(control, "settings-control");
        }
        gtk_widget_set_valign(control, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(row), control);
    }
    return row;
}

GtkWidget* makeCombo(const std::vector<const char*>& labels, int active)
{
    GtkWidget* combo = gtk_combo_box_text_new();
    for (const char* label : labels)
    {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), label);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), active);
    return combo;
}

GtkWidget* makeSpin(double min, double max, double step, double value)
{
    GtkWidget* spin = gtk_spin_button_new_with_range(min, max, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    return spin;
}

GtkWidget* makeSwitch(bool active)
{
    GtkWidget* sw = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(sw), active);
    gtk_widget_add_css_class(sw, "settings-switch");
    gtk_widget_set_halign(sw, GTK_ALIGN_END);
    gtk_widget_set_size_request(sw, 48, -1);
    return sw;
}

GtkWidget* wrapSettingsGroup(GtkWidget* section)
{
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), section);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    return scroll;
}

GtkWidget* addSettingsGroup(GtkWidget* stack,
                            const char* name,
                            const char* title,
                            GtkWidget* section)
{
    GtkWidget* page = wrapSettingsGroup(section);
    gtk_stack_add_titled(GTK_STACK(stack),
                         page,
                         name,
                         title);
    return page;
}

GtkWidget* launchSettingsLayout(GtkUConsoleAppState& state)
{
    GtkWidget* root = makeWorkbench(GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(actions, "settings-actions");
    state.settings_status = makeLabel("", "settings-status", true);
    gtk_widget_set_hexpand(state.settings_status, TRUE);
    gtk_box_append(GTK_BOX(actions), state.settings_status);
    GtkWidget* reload = gtk_button_new_with_label("Reload");
    gtk_widget_add_css_class(reload, "nav-button");
    g_signal_connect(reload, "clicked", G_CALLBACK(onSettingsReloadClicked),
                     &state);
    gtk_box_append(GTK_BOX(actions), reload);
    GtkWidget* apply = gtk_button_new_with_label("Save");
    gtk_widget_add_css_class(apply, "send");
    g_signal_connect(apply, "clicked", G_CALLBACK(onSettingsApplyClicked),
                     &state);
    gtk_box_append(GTK_BOX(actions), apply);
    gtk_box_append(GTK_BOX(root), actions);

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(body, "settings-body");
    gtk_widget_set_hexpand(body, TRUE);
    gtk_widget_set_vexpand(body, TRUE);

    state.settings_group_stack = gtk_stack_new();
    gtk_widget_set_hexpand(state.settings_group_stack, TRUE);
    gtk_widget_set_vexpand(state.settings_group_stack, TRUE);

    GtkWidget* sidebar = gtk_stack_sidebar_new();
    gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(sidebar),
                                GTK_STACK(state.settings_group_stack));
    gtk_widget_add_css_class(sidebar, "settings-sidebar");
    gtk_widget_set_vexpand(sidebar, TRUE);
    gtk_widget_set_size_request(sidebar, 160, -1);
    gtk_box_append(GTK_BOX(body), sidebar);
    gtk_box_append(GTK_BOX(body), state.settings_group_stack);

    state.settings_page_box = body;

    const auto& config = state.services.config();
    const auto& mesh = config.activeMeshConfig();
    const auto protocol = config.mesh_protocol;
    const auto mqtt = loadMqttSettings();

    GtkWidget* identity = makeSettingsSection("Identity");
    state.settings_node_name = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_node_name),
                          config.node_name);
    gtk_box_append(GTK_BOX(identity),
                   makeSettingsRow("Node name",
                                   "Broadcast name stored in local config.",
                                   state.settings_node_name));
    state.settings_short_name = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_short_name),
                          config.short_name);
    gtk_box_append(GTK_BOX(identity),
                   makeSettingsRow("Short name",
                                   "Compact display name for status and packets.",
                                   state.settings_short_name));
    addSettingsGroup(state.settings_group_stack,
                     "identity",
                     "Identity",
                     identity);

    GtkWidget* protocol_group = makeSettingsSection("Protocol");
    std::vector<const char*> protocols{
        ::chat::infra::meshProtocolName(::chat::MeshProtocol::Meshtastic),
        ::chat::infra::meshProtocolName(::chat::MeshProtocol::MeshCore),
        ::chat::infra::meshProtocolName(::chat::MeshProtocol::RNode),
        ::chat::infra::meshProtocolName(::chat::MeshProtocol::LXMF),
    };
    state.settings_protocol =
        makeCombo(protocols, protocolIndex(config.mesh_protocol));
    g_signal_connect(state.settings_protocol,
                     "changed",
                     G_CALLBACK(onSettingsProtocolChanged),
                     &state);
    gtk_box_append(GTK_BOX(protocol_group),
                   makeSettingsRow("Protocol",
                                   "Active mesh protocol used by chat transport.",
                                   state.settings_protocol));
    addSettingsGroup(state.settings_group_stack,
                     "protocol",
                     "Protocol",
                     protocol_group);

    GtkWidget* meshtastic = makeSettingsSection("Meshtastic");
    state.settings_lora_region =
        makeCombo(meshtasticRegionLabels(),
                  meshtasticRegionIndex(mesh.region));
    g_signal_connect(state.settings_lora_region,
                     "changed",
                     G_CALLBACK(onSettingsMeshtasticRegionChanged),
                     &state);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Region",
                                   "Meshtastic legal radio region.",
                                   state.settings_lora_region));
    state.settings_lora_use_preset = makeSwitch(mesh.use_preset);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Use modem preset",
                                   "Uses modem preset instead of only manual LoRa parameters.",
                                   state.settings_lora_use_preset));
    state.settings_lora_modem_preset =
        makeCombo(meshtasticPresetLabels(),
                  meshtasticPresetIndex(mesh.modem_preset));
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Modem preset",
                                   "Meshtastic speed/range preset.",
                                   state.settings_lora_modem_preset));

    GtkWidget* link = makeSettingsSection("LoRa link");
    state.settings_lora_freq =
        makeSpin(137.0, 1020.0, 0.001, displayFrequencyMhz(mesh));
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(state.settings_lora_freq), 3);
    gtk_box_append(GTK_BOX(link),
                   makeSettingsRow("Frequency MHz",
                                   "SX1262 carrier frequency.",
                                   state.settings_lora_freq));
    state.settings_lora_bw =
        makeSpin(7.8, 500.0, 1.0, displayBandwidthKHz(mesh, protocol));
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(state.settings_lora_bw), 1);
    gtk_box_append(GTK_BOX(link),
                   makeSettingsRow("Bandwidth kHz",
                                   "LoRa bandwidth written into active mesh config.",
                                   state.settings_lora_bw));
    state.settings_lora_sf =
        makeSpin(5.0, 12.0, 1.0, displaySpreadFactor(mesh, protocol));
    gtk_box_append(GTK_BOX(link),
                   makeSettingsRow("Spread factor",
                                   "LoRa SF, persisted with the active protocol.",
                                   state.settings_lora_sf));
    state.settings_lora_cr =
        makeSpin(5.0, 8.0, 1.0, displayCodingRate(mesh, protocol));
    gtk_box_append(GTK_BOX(link),
                   makeSettingsRow("Coding rate",
                                   "LoRa coding rate denominator.",
                                   state.settings_lora_cr));
    state.settings_lora_tx = makeSpin(::app::AppConfig::kTxPowerMinDbm,
                                      ::app::AppConfig::kTxPowerMaxDbm,
                                      1.0,
                                      mesh.tx_power);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("TX power dBm",
                                   "Transmit power clamped to build target limits.",
                                   state.settings_lora_tx));
    state.settings_hop_limit = makeSpin(1.0, 7.0, 1.0, mesh.hop_limit);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Hop limit",
                                   "Maximum relay hop count for outgoing packets.",
                                   state.settings_hop_limit));
    state.settings_tx_enabled = makeSwitch(mesh.tx_enabled);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Transmit",
                                   "Controls whether the active mesh config allows TX.",
                                   state.settings_tx_enabled));
    state.settings_lora_channel_num =
        makeSpin(0.0, 65535.0, 1.0, mesh.channel_num);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Channel number",
                                   "0 means automatic channel hash.",
                                   state.settings_lora_channel_num));
    state.settings_lora_freq_offset =
        makeSpin(-10.0, 10.0, 0.001, mesh.frequency_offset_mhz);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(state.settings_lora_freq_offset),
                               3);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Frequency offset MHz",
                                   "Fine offset applied to the selected channel.",
                                   state.settings_lora_freq_offset));
    state.settings_lora_override_duty =
        makeSwitch(mesh.override_duty_cycle);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Override duty cycle",
                                   "Protocol-level duty-cycle override flag.",
                                   state.settings_lora_override_duty));
    state.settings_lora_ignore_mqtt = makeSwitch(mesh.ignore_mqtt);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("Ignore MQTT packets",
                                   "Drops packets marked as arriving through MQTT.",
                                   state.settings_lora_ignore_mqtt));
    state.settings_lora_ok_to_mqtt =
        makeSwitch(mesh.config_ok_to_mqtt);
    gtk_box_append(GTK_BOX(meshtastic),
                   makeSettingsRow("OK to MQTT",
                                   "Sets the ok_to_mqtt bit on outgoing packets.",
                                   state.settings_lora_ok_to_mqtt));
    state.settings_meshtastic_page =
        addSettingsGroup(state.settings_group_stack,
                         "meshtastic",
                         "Meshtastic",
                         meshtastic);
    state.settings_link_page =
        addSettingsGroup(state.settings_group_stack, "link", "LoRa link", link);

    GtkWidget* meshcore = makeSettingsSection("MeshCore");
    state.settings_meshcore_region_preset =
        makeSpin(0.0, 255.0, 1.0, mesh.meshcore_region_preset);
    gtk_box_append(GTK_BOX(meshcore),
                   makeSettingsRow("MeshCore region preset",
                                   "0 keeps MeshCore in custom frequency mode.",
                                   state.settings_meshcore_region_preset));
    state.settings_meshcore_channel_slot =
        makeSpin(0.0, 255.0, 1.0, mesh.meshcore_channel_slot);
    gtk_box_append(GTK_BOX(meshcore),
                   makeSettingsRow("MeshCore channel slot",
                                   "MeshCore channel slot persisted per protocol.",
                                   state.settings_meshcore_channel_slot));
    state.settings_meshcore_channel_name = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_meshcore_channel_name),
                          mesh.meshcore_channel_name);
    gtk_box_append(GTK_BOX(meshcore),
                   makeSettingsRow("MeshCore channel name",
                                   "MeshCore channel label.",
                                   state.settings_meshcore_channel_name));
    state.settings_meshcore_client_repeat =
        makeSwitch(mesh.meshcore_client_repeat);
    gtk_box_append(GTK_BOX(meshcore),
                   makeSettingsRow("MeshCore client repeat",
                                   "Client repeat flag for MeshCore.",
                                   state.settings_meshcore_client_repeat));
    state.settings_meshcore_rx_delay =
        makeSpin(0.0, 60.0, 0.1, mesh.meshcore_rx_delay_base);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(state.settings_meshcore_rx_delay),
                               1);
    gtk_box_append(GTK_BOX(meshcore),
                   makeSettingsRow("MeshCore RX delay",
                                   "Base receive delay value.",
                                   state.settings_meshcore_rx_delay));
    state.settings_meshcore_airtime =
        makeSpin(0.1, 20.0, 0.1, mesh.meshcore_airtime_factor);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(state.settings_meshcore_airtime),
                               1);
    gtk_box_append(GTK_BOX(meshcore),
                   makeSettingsRow("MeshCore airtime factor",
                                   "Airtime factor used by MeshCore flood logic.",
                                   state.settings_meshcore_airtime));
    state.settings_meshcore_flood_max =
        makeSpin(0.0, 255.0, 1.0, mesh.meshcore_flood_max);
    gtk_box_append(GTK_BOX(meshcore),
                   makeSettingsRow("MeshCore flood max",
                                   "Maximum MeshCore flood window.",
                                   state.settings_meshcore_flood_max));
    state.settings_meshcore_multi_acks =
        makeSwitch(mesh.meshcore_multi_acks);
    gtk_box_append(GTK_BOX(meshcore),
                   makeSettingsRow("MeshCore multi ACKs",
                                   "Enables MeshCore multi-ACK behaviour.",
                                   state.settings_meshcore_multi_acks));
    state.settings_meshcore_page =
        addSettingsGroup(state.settings_group_stack,
                         "meshcore",
                         "MeshCore",
                         meshcore);

    GtkWidget* channels = makeSettingsSection("Channels");
    state.settings_primary_enabled = makeSwitch(config.primary_enabled);
    gtk_box_append(GTK_BOX(channels),
                   makeSettingsRow("Primary enabled",
                                   "Enables primary mesh channel.",
                                   state.settings_primary_enabled));
    state.settings_secondary_enabled = makeSwitch(config.secondary_enabled);
    gtk_box_append(GTK_BOX(channels),
                   makeSettingsRow("Secondary enabled",
                                   "Enables secondary mesh channel.",
                                   state.settings_secondary_enabled));
    state.settings_primary_uplink =
        makeSwitch(config.primary_uplink_enabled);
    gtk_box_append(GTK_BOX(channels),
                   makeSettingsRow("Primary MQTT uplink",
                                   "Allows primary channel uplink through MQTT proxy.",
                                   state.settings_primary_uplink));
    state.settings_primary_downlink =
        makeSwitch(config.primary_downlink_enabled);
    gtk_box_append(GTK_BOX(channels),
                   makeSettingsRow("Primary MQTT downlink",
                                   "Allows primary channel downlink from MQTT proxy.",
                                   state.settings_primary_downlink));
    state.settings_secondary_uplink =
        makeSwitch(config.secondary_uplink_enabled);
    gtk_box_append(GTK_BOX(channels),
                   makeSettingsRow("Secondary MQTT uplink",
                                   "Allows secondary channel uplink through MQTT proxy.",
                                   state.settings_secondary_uplink));
    state.settings_secondary_downlink =
        makeSwitch(config.secondary_downlink_enabled);
    gtk_box_append(GTK_BOX(channels),
                   makeSettingsRow("Secondary MQTT downlink",
                                   "Allows secondary channel downlink from MQTT proxy.",
                                   state.settings_secondary_downlink));
    addSettingsGroup(state.settings_group_stack,
                     "channels",
                     "Channels",
                     channels);

    GtkWidget* chat = makeSettingsSection("Chat / Policy");
    state.settings_chat_channel =
        makeCombo({"Primary", "Secondary"},
                  std::clamp<int>(config.chat_channel, 0, 1));
    gtk_box_append(GTK_BOX(chat),
                   makeSettingsRow("Default channel",
                                   "Channel used by outgoing chat messages.",
                                   state.settings_chat_channel));
    state.settings_relay_enabled =
        makeSwitch(config.chat_policy.enable_relay);
    gtk_box_append(GTK_BOX(chat),
                   makeSettingsRow("Relay",
                                   "Allows this node to forward mesh traffic.",
                                   state.settings_relay_enabled));
    state.settings_ack_broadcast =
        makeSwitch(config.chat_policy.ack_for_broadcast);
    gtk_box_append(GTK_BOX(chat),
                   makeSettingsRow("Broadcast ACK",
                                   "Requests ACK for broadcast messages.",
                                   state.settings_ack_broadcast));
    state.settings_ack_squad = makeSwitch(config.chat_policy.ack_for_squad);
    gtk_box_append(GTK_BOX(chat),
                   makeSettingsRow("Squad ACK",
                                   "Requests ACK for direct or squad messages.",
                                   state.settings_ack_squad));
    state.settings_tx_retries =
        makeSpin(0.0, 5.0, 1.0, config.chat_policy.max_tx_retries);
    gtk_box_append(GTK_BOX(chat),
                   makeSettingsRow("TX retries",
                                   "Retry budget used by chat policy.",
                                   state.settings_tx_retries));
    state.settings_max_channels =
        makeSpin(1.0, 3.0, 1.0, config.chat_policy.max_channels);
    gtk_box_append(GTK_BOX(chat),
                   makeSettingsRow("Max channels",
                                   "Maximum chat channels exposed by policy.",
                                   state.settings_max_channels));
    addSettingsGroup(state.settings_group_stack,
                     "chat",
                     "Chat",
                     chat);

    GtkWidget* gps = makeSettingsSection("GPS");
    state.settings_gps_enabled = makeSwitch(config.gps_enabled);
    gtk_box_append(GTK_BOX(gps),
                   makeSettingsRow("GPS enabled",
                                   "Applies to the Linux GPS runtime.",
                                   state.settings_gps_enabled));
    state.settings_gps_interval =
        makeSpin(1000.0, 3600000.0, 1000.0, config.gps_interval_ms);
    gtk_box_append(GTK_BOX(gps),
                   makeSettingsRow("Interval ms",
                                   "GPS collection interval persisted in AppConfig.",
                                   state.settings_gps_interval));
    state.settings_gps_mode =
        makeCombo({"High accuracy", "Power save", "Fix only"},
                  std::clamp<int>(config.gps_mode, 0, 2));
    gtk_box_append(GTK_BOX(gps),
                   makeSettingsRow("Location mode",
                                   "GNSS mode applied to the Linux GPS runtime.",
                                   state.settings_gps_mode));
    state.settings_gps_strategy =
        makeCombo({"Continuous", "Motion wake", "Low power off"},
                  std::clamp<int>(config.gps_strategy, 0, 2));
    gtk_box_append(GTK_BOX(gps),
                   makeSettingsRow("Position strategy",
                                   "Power strategy used by GPS collection.",
                                   state.settings_gps_strategy));
    state.settings_external_nmea_hz =
        makeSpin(0.0, 10.0, 1.0, config.external_nmea_output_hz);
    gtk_box_append(GTK_BOX(gps),
                   makeSettingsRow("NMEA export Hz",
                                   "0 disables external NMEA output.",
                                   state.settings_external_nmea_hz));
    state.settings_external_nmea_mask =
        makeSpin(0.0, 255.0, 1.0, config.external_nmea_sentence_mask);
    gtk_box_append(GTK_BOX(gps),
                   makeSettingsRow("NMEA sentence mask",
                                   "Raw sentence mask persisted for GPS output.",
                                   state.settings_external_nmea_mask));
    addSettingsGroup(state.settings_group_stack, "gps", "GPS", gps);

    GtkWidget* map = makeSettingsSection("Map");
    state.settings_map_source = makeCombo({"OSM", "Terrain", "Satellite"},
                                          std::clamp<int>(config.map_source,
                                                          0,
                                                          2));
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("Base map",
                                   "Default map source for the uConsole map view.",
                                   state.settings_map_source));
    state.settings_map_zoom =
        makeSpin(1.0, 18.0, 1.0, state.map_model.snapshot().zoom);
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("Zoom",
                                   "Map zoom used by the GTK map canvas.",
                                   state.settings_map_zoom));
    state.settings_map_contour = makeSwitch(config.map_contour_enabled);
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("Contour overlay",
                                   "Shows transparent contour PNG tiles above the selected base map.",
                                   state.settings_map_contour));
    state.settings_map_contour_ultra_fine =
        makeSwitch(state.map_model.contourUltraFineEnabled());
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("Ultra-fine contours",
                                   "Adds 5 m minor contours at z17+ for visible-fill generation and cached overlays.",
                                   state.settings_map_contour_ultra_fine));
    GtkWidget* earthdata_token_control =
        gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(earthdata_token_control,
                             "settings-inline-control");
    state.settings_map_earthdata_token = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(state.settings_map_earthdata_token),
                             FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(state.settings_map_earthdata_token),
                                GTK_INPUT_PURPOSE_PASSWORD);
    gtk_widget_set_focusable(state.settings_map_earthdata_token, TRUE);
    gtk_widget_set_hexpand(state.settings_map_earthdata_token, TRUE);
    {
        const auto token = state.map_model.earthdataToken();
        gtk_editable_set_text(GTK_EDITABLE(state.settings_map_earthdata_token),
                              token.c_str());
    }
    gtk_box_append(GTK_BOX(earthdata_token_control),
                   state.settings_map_earthdata_token);
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("Earthdata token",
                                   "Stored in local SQLite for NASADEM contour generation and cache workflows.",
                                   earthdata_token_control));
    state.settings_map_track = makeSwitch(config.map_track_enabled);
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("Track recording",
                                   "Persists local track recording preference.",
                                   state.settings_map_track));
    state.settings_map_mqtt_nodes =
        makeSwitch(state.map_model.snapshot().show_mqtt_nodes);
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("MQTT node layer",
                                   "Shows positioned nodes that arrived through MQTT.",
                                   state.settings_map_mqtt_nodes));
    state.settings_map_track_interval =
        makeSpin(1.0, 99.0, 1.0, config.map_track_interval);
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("Track interval",
                                   "Track interval seconds, 99 means distance mode.",
                                   state.settings_map_track_interval));
    state.settings_map_track_format =
        makeCombo({"GPX", "CSV", "Binary"},
                  std::clamp<int>(config.map_track_format, 0, 2));
    gtk_box_append(GTK_BOX(map),
                   makeSettingsRow("Track format",
                                   "Format for future local track exports.",
                                   state.settings_map_track_format));
    addSettingsGroup(state.settings_group_stack, "map", "Map", map);

    GtkWidget* mqtt_section = makeSettingsSection("MQTT");
    state.settings_mqtt_enabled = makeSwitch(mqtt.enabled);
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Enabled",
                                   "Enables the Linux MQTT source definition.",
                                   state.settings_mqtt_enabled));
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Runtime",
                                   "Current broker client state.",
                                   makeLabel("Client offline", "settings-status")));
    state.settings_mqtt_name = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_name),
                          mqtt.name.c_str());
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Source name",
                                   "Local display name for this MQTT source.",
                                   state.settings_mqtt_name));
    state.settings_mqtt_host = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_host),
                          mqtt.host.c_str());
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Host",
                                   "Broker host, for example mqtt.mess.host.",
                                   state.settings_mqtt_host));
    state.settings_mqtt_port =
        makeSpin(1.0, 65535.0, 1.0, mqtt.port);
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Port",
                                   "Broker TCP port.",
                                   state.settings_mqtt_port));
    state.settings_mqtt_username = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_username),
                          mqtt.username.c_str());
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Username",
                                   "MQTT username.",
                                   state.settings_mqtt_username));
    state.settings_mqtt_password = gtk_password_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_password),
                          mqtt.password.c_str());
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Password",
                                   "MQTT password stored in local SQLite settings.",
                                   state.settings_mqtt_password));
    state.settings_mqtt_topic = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_topic),
                          mqtt.topic.c_str());
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Topic",
                                   "Subscribe topic such as msh/CN/#.",
                                   state.settings_mqtt_topic));
    state.settings_mqtt_tls = makeSwitch(mqtt.tls);
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("TLS",
                                   "Use TLS for the MQTT broker connection.",
                                   state.settings_mqtt_tls));
    state.settings_mqtt_client_id = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(state.settings_mqtt_client_id),
                          mqtt.client_id.c_str());
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Client ID",
                                   "Empty lets the runtime generate a client id.",
                                   state.settings_mqtt_client_id));
    state.settings_mqtt_clean_session = makeSwitch(mqtt.clean_session);
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Clean session",
                                   "MQTT clean session flag.",
                                   state.settings_mqtt_clean_session));
    state.settings_mqtt_qos = makeSpin(0.0, 2.0, 1.0, mqtt.qos);
    gtk_box_append(GTK_BOX(mqtt_section),
                   makeSettingsRow("Subscribe QoS",
                                   "0, 1, or 2.",
                                   state.settings_mqtt_qos));
    addSettingsGroup(state.settings_group_stack,
                     "mqtt",
                     "MQTT",
                     mqtt_section);

    GtkWidget* network = makeSettingsSection("Network / Privacy");
    state.settings_net_duty_cycle = makeSwitch(config.net_duty_cycle);
    gtk_box_append(GTK_BOX(network),
                   makeSettingsRow("Duty cycle limits",
                                   "Keeps airtime throttling enabled for normal TX.",
                                   state.settings_net_duty_cycle));
    state.settings_net_channel_util =
        makeSpin(0.0, 100.0, 25.0, config.net_channel_util);
    gtk_box_append(GTK_BOX(network),
                   makeSettingsRow("Channel utilization %",
                                   "0 leaves utilization control automatic.",
                                   state.settings_net_channel_util));
    state.settings_privacy_encrypt_mode =
        makeCombo({"Off", "PSK", "PKI"},
                  std::clamp<int>(config.privacy_encrypt_mode, 0, 2));
    gtk_box_append(GTK_BOX(network),
                   makeSettingsRow("Encryption mode",
                                   "Persists local privacy mode selection.",
                                   state.settings_privacy_encrypt_mode));
    addSettingsGroup(state.settings_group_stack,
                     "network",
                     "Network",
                     network);

    gtk_stack_set_visible_child_name(GTK_STACK(state.settings_group_stack),
                                     "identity");
    gtk_box_append(GTK_BOX(root), body);
    showSettingsNotice(state, "Settings loaded.");
    return root;
}

} // namespace trailmate::uconsole::gtk
