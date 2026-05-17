#pragma once

#include "platform/gtk/gtk_uconsole_app.h"

#include <chrono>
#include <cstddef>
#include <future>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtk/gtk.h>

#include "app/linux_app_services.h"
#include "gps/usecase/gnss_skyplot_presenter.h"
#include "platform/linux/runtime_packet_log.h"
#include "uconsole/uconsole_chat_workspace_model.h"
#include "uconsole/uconsole_dashboard_model.h"
#include "uconsole/uconsole_map_workspace_model.h"

namespace trailmate::uconsole::gtk
{

constexpr std::size_t kConversationLimit = 12;
constexpr std::size_t kMessageLimit = 28;
constexpr std::size_t kMaxConcurrentMapFetches = 6;
constexpr int kMapFetchRetryBaseSeconds = 2;
constexpr int kMapFetchRetryMaxSeconds = 60;

struct GtkUConsoleAppState;

struct GtkUConsoleRefreshSnapshot
{
    UConsoleDashboardSnapshot dashboard{};
    MapWorkspaceSnapshot map{};
};

using GtkUConsoleLaunchFn = GtkWidget* (*)(GtkUConsoleAppState&);
using GtkUConsoleShowFn = void (*)(GtkUConsoleAppState&);
using GtkUConsoleHideFn = void (*)(GtkUConsoleAppState&);
using GtkUConsoleRefreshFn = void (*)(GtkUConsoleAppState&,
                                      const GtkUConsoleRefreshSnapshot&);
using GtkUConsoleDestroyFn = void (*)(GtkUConsoleAppState&);

struct GtkUConsolePageLifecycle
{
    const char* name = "";
    const char* title = "";
    GtkUConsoleLaunchFn onLaunch = nullptr;
    GtkUConsoleShowFn onShow = nullptr;
    GtkUConsoleHideFn onHide = nullptr;
    GtkUConsoleRefreshFn onRefresh = nullptr;
    GtkUConsoleDestroyFn onDestroy = nullptr;
};

struct GtkUConsoleAppState
{
    explicit GtkUConsoleAppState(GtkUConsoleOptions options_in)
        : options(std::move(options_in)),
          services({.default_node_name = "Trail Mate uConsole",
                    .default_short_name = "TU",
                    .demo_broadcast_text =
                        "Broadcast: Trail Mate uConsole local mesh online."}),
          dashboard_model(services),
          chat_model(services),
          map_model(services)
    {
    }

    GtkUConsoleOptions options{};
    linux_app::LinuxAppServices services;
    UConsoleDashboardModel dashboard_model;
    UConsoleChatWorkspaceModel chat_model;
    UConsoleMapWorkspaceModel map_model;

    GtkWidget* window = nullptr;
    GtkWidget* stack = nullptr;
    GtkWidget* nav_overview = nullptr;
    GtkWidget* nav_chat = nullptr;
    GtkWidget* nav_chat_badge = nullptr;
    GtkWidget* nav_map = nullptr;
    GtkWidget* nav_hardware = nullptr;
    GtkWidget* nav_data = nullptr;
    GtkWidget* nav_logs = nullptr;
    GtkWidget* nav_settings = nullptr;

    GtkWidget* status_aio2 = nullptr;
    GtkWidget* status_lora = nullptr;
    GtkWidget* status_gps = nullptr;
    GtkWidget* status_node = nullptr;
    GtkWidget* status_unread = nullptr;

    GtkWidget* overview_location_panel = nullptr;
    GtkWidget* overview_location_state = nullptr;
    GtkWidget* overview_location_coordinates = nullptr;
    GtkWidget* overview_location_detail = nullptr;
    GtkWidget* overview_location_map_meta = nullptr;
    GtkWidget* overview_location_map = nullptr;
    GtkWidget* overview_gnss_skyplot = nullptr;
    GtkWidget* overview_satellite_list = nullptr;
    GtkWidget* overview_messages_panel = nullptr;
    GtkWidget* overview_messages_title = nullptr;
    GtkWidget* overview_messages_detail = nullptr;
    GtkWidget* overview_messages_latest = nullptr;
    GtkWidget* hardware_box = nullptr;
    GtkWidget* overview_conversations = nullptr;
    GtkWidget* team_summary = nullptr;
    GtkWidget* team_timeline_box = nullptr;
    GtkWidget* overview_timeline_filter = nullptr;
    GtkWidget* overview_timeline_scroll = nullptr;
    GtkWidget* overview_timeline_box = nullptr;
    GtkWidget* capability_box = nullptr;
    GtkWidget* hardware_page_box = nullptr;
    GtkWidget* data_page_box = nullptr;
    GtkWidget* logs_page_box = nullptr;
    GtkWidget* logs_source_gps = nullptr;
    GtkWidget* logs_source_lora = nullptr;
    GtkWidget* logs_source_mqtt = nullptr;
    GtkWidget* settings_page_box = nullptr;
    GtkWidget* settings_group_stack = nullptr;
    GtkWidget* settings_meshtastic_page = nullptr;
    GtkWidget* settings_link_page = nullptr;
    GtkWidget* settings_meshcore_page = nullptr;
    GtkWidget* settings_node_name = nullptr;
    GtkWidget* settings_short_name = nullptr;
    GtkWidget* settings_protocol = nullptr;
    GtkWidget* settings_lora_region = nullptr;
    GtkWidget* settings_lora_use_preset = nullptr;
    GtkWidget* settings_lora_modem_preset = nullptr;
    GtkWidget* settings_lora_freq = nullptr;
    GtkWidget* settings_lora_bw = nullptr;
    GtkWidget* settings_lora_sf = nullptr;
    GtkWidget* settings_lora_cr = nullptr;
    GtkWidget* settings_lora_tx = nullptr;
    GtkWidget* settings_hop_limit = nullptr;
    GtkWidget* settings_tx_enabled = nullptr;
    GtkWidget* settings_lora_channel_num = nullptr;
    GtkWidget* settings_lora_freq_offset = nullptr;
    GtkWidget* settings_lora_override_duty = nullptr;
    GtkWidget* settings_lora_ignore_mqtt = nullptr;
    GtkWidget* settings_lora_ok_to_mqtt = nullptr;
    GtkWidget* settings_meshcore_region_preset = nullptr;
    GtkWidget* settings_meshcore_channel_slot = nullptr;
    GtkWidget* settings_meshcore_channel_name = nullptr;
    GtkWidget* settings_meshcore_client_repeat = nullptr;
    GtkWidget* settings_meshcore_rx_delay = nullptr;
    GtkWidget* settings_meshcore_airtime = nullptr;
    GtkWidget* settings_meshcore_flood_max = nullptr;
    GtkWidget* settings_meshcore_multi_acks = nullptr;
    GtkWidget* settings_primary_enabled = nullptr;
    GtkWidget* settings_secondary_enabled = nullptr;
    GtkWidget* settings_primary_uplink = nullptr;
    GtkWidget* settings_primary_downlink = nullptr;
    GtkWidget* settings_secondary_uplink = nullptr;
    GtkWidget* settings_secondary_downlink = nullptr;
    GtkWidget* settings_chat_channel = nullptr;
    GtkWidget* settings_relay_enabled = nullptr;
    GtkWidget* settings_ack_broadcast = nullptr;
    GtkWidget* settings_ack_squad = nullptr;
    GtkWidget* settings_tx_retries = nullptr;
    GtkWidget* settings_max_channels = nullptr;
    GtkWidget* settings_gps_enabled = nullptr;
    GtkWidget* settings_gps_interval = nullptr;
    GtkWidget* settings_gps_mode = nullptr;
    GtkWidget* settings_gps_strategy = nullptr;
    GtkWidget* settings_external_nmea_hz = nullptr;
    GtkWidget* settings_external_nmea_mask = nullptr;
    GtkWidget* settings_map_source = nullptr;
    GtkWidget* settings_map_zoom = nullptr;
    GtkWidget* settings_map_contour = nullptr;
    GtkWidget* settings_map_contour_ultra_fine = nullptr;
    GtkWidget* settings_map_earthdata_token = nullptr;
    GtkWidget* settings_map_track = nullptr;
    GtkWidget* settings_map_mqtt_nodes = nullptr;
    GtkWidget* settings_map_track_interval = nullptr;
    GtkWidget* settings_map_track_format = nullptr;
    GtkWidget* settings_mqtt_enabled = nullptr;
    GtkWidget* settings_mqtt_name = nullptr;
    GtkWidget* settings_mqtt_host = nullptr;
    GtkWidget* settings_mqtt_port = nullptr;
    GtkWidget* settings_mqtt_username = nullptr;
    GtkWidget* settings_mqtt_password = nullptr;
    GtkWidget* settings_mqtt_topic = nullptr;
    GtkWidget* settings_mqtt_tls = nullptr;
    GtkWidget* settings_mqtt_client_id = nullptr;
    GtkWidget* settings_mqtt_clean_session = nullptr;
    GtkWidget* settings_mqtt_qos = nullptr;
    GtkWidget* settings_net_duty_cycle = nullptr;
    GtkWidget* settings_net_channel_util = nullptr;
    GtkWidget* settings_privacy_encrypt_mode = nullptr;
    GtkWidget* settings_status = nullptr;
    ::platform::linux_runtime::PacketLogSource logs_source =
        ::platform::linux_runtime::PacketLogSource::Lora;

    GtkWidget* chat_conversation_list = nullptr;
    GtkWidget* chat_sort_combo = nullptr;
    GtkWidget* chat_message_scroll = nullptr;
    GtkWidget* chat_message_list = nullptr;
    GtkWidget* chat_title = nullptr;
    GtkWidget* chat_meta = nullptr;
    GtkWidget* chat_node_box = nullptr;
    GtkWidget* chat_status = nullptr;
    GtkWidget* chat_entry = nullptr;
    GtkWidget* chat_send_button = nullptr;
    GtkWidget* chat_add_contact_button = nullptr;
    GtkWidget* chat_request_nodeinfo_button = nullptr;
    GtkWidget* chat_send_position_button = nullptr;
    GtkWidget* chat_send_poi_button = nullptr;
    ChatThreadSortMode chat_sort_mode = ChatThreadSortMode::Recent;
    std::map<std::string, bool> chat_group_expanded{};
    std::string chat_message_signature{};

    GtkWidget* map_title = nullptr;
    GtkWidget* map_meta = nullptr;
    GtkWidget* map_canvas = nullptr;
    GtkWidget* map_marker_layer = nullptr;
    GtkWidget* map_grid = nullptr;
    GtkWidget* map_contour_grid = nullptr;
    GtkWidget* map_status = nullptr;
    GtkWidget* map_cache_status = nullptr;
    GtkWidget* map_source_osm = nullptr;
    GtkWidget* map_source_terrain = nullptr;
    GtkWidget* map_source_satellite = nullptr;
    GtkWidget* map_mqtt_nodes = nullptr;
    GtkWidget* map_contour_visible = nullptr;
    GtkWidget* map_contour_fill = nullptr;
    GtkWidget* map_contour_status = nullptr;
    GtkWidget* map_measure_button = nullptr;
    GtkWidget* map_measure_clear = nullptr;
    GtkWidget* map_measure_status = nullptr;
    GtkWidget* map_recenter = nullptr;
    GtkWidget* map_context_popover = nullptr;
    GtkWidget* map_context_label = nullptr;
    double map_context_lat = 0.0;
    double map_context_lon = 0.0;
    bool map_context_valid = false;
    std::uint32_t map_selected_node_id = 0;
    double map_drag_start_lat = 0.0;
    double map_drag_start_lon = 0.0;
    int map_drag_start_zoom = 14;
    bool map_dragging = false;
    bool map_measure_enabled = false;
    bool map_measure_has_start = false;
    bool map_measure_has_end = false;
    double map_measure_start_lat = 0.0;
    double map_measure_start_lon = 0.0;
    double map_measure_end_lat = 0.0;
    double map_measure_end_lon = 0.0;
    ::gps::GnssSkyplotView overview_gnss_view{};

    struct MapFetchJob
    {
        std::string key{};
        ::platform::linux_runtime::MapTileId tile{};
        std::future<::platform::linux_runtime::MapTileResult> future{};
    };

    struct MapFetchRetryState
    {
        unsigned attempts = 0;
        std::chrono::steady_clock::time_point next_retry{};
        std::string last_error{};
    };

    struct MapContourFillJob
    {
        std::future<::platform::linux_runtime::MapContourGenerationResult>
            future{};
    };

    std::vector<MapFetchJob> map_fetch_jobs{};
    std::set<std::string> map_inflight_tiles{};
    std::map<std::string, MapFetchRetryState> map_failed_tiles{};
    MapContourFillJob contour_fill_job{};
    std::string map_fetch_status{};
    std::string contour_fill_status{};
    std::string map_grid_signature{};
    int overview_timeline_filter_index = 0;
    std::string settings_notice{};
    int settings_notice_ticks = 0;

    std::vector<GtkUConsolePageLifecycle> page_lifecycle{};
    std::string active_page{};
    guint refresh_source = 0;
    bool shutdown_complete = false;
};

} // namespace trailmate::uconsole::gtk
