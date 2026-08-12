#include "platform/gtk/gtk_uconsole_layout_spec.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

#include "gps/usecase/gnss_skyplot_presenter.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/tracker_runtime.h"

namespace trailmate::uconsole::gtk
{
namespace
{

constexpr std::size_t kFieldConversationLimit = 64;
constexpr std::size_t kFieldSatelliteLimit = 32;

::chat::NodeId nodeIdFromButton(GtkButton* button)
{
    return static_cast<::chat::NodeId>(GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(button), "trailmate-node-id")));
}

GtkWidget* makeFieldAction(const char* label,
                           const ChatNodeInfoItem& item,
                           GCallback callback,
                           GtkUConsoleAppState& state,
                           bool enabled = true)
{
    GtkWidget* button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "chat-node-action");
    g_object_set_data(G_OBJECT(button),
                      "trailmate-node-id",
                      GUINT_TO_POINTER(static_cast<guint>(item.node_id)));
    gtk_widget_set_sensitive(button, enabled ? TRUE : FALSE);
    g_signal_connect(button, "clicked", callback, &state);
    return button;
}

void onFieldNodeChatClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.selectNodeConversation(nodeIdFromButton(button));
    showPage(state, "chat");
}

GtkWidget* makeContactNodeRow(GtkUConsoleAppState& state,
                              const ChatNodeInfoItem& item)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(row, "field-contact-row");

    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* title = makeLabel(item.title.c_str(), "row-title", true);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(title_row), title);
    gtk_box_append(GTK_BOX(title_row),
                   makeLabel(item.via_mqtt ? "MQTT" : "LoRa", "mini-chip"));
    gtk_box_append(GTK_BOX(row), title_row);
    gtk_box_append(GTK_BOX(row),
                   makeLabel(item.subtitle.c_str(), "row-meta", true));
    gtk_box_append(GTK_BOX(row),
                   makeLabel(item.status.c_str(), "row-meta", true));
    gtk_box_append(GTK_BOX(row),
                   makeLabel(item.signal.c_str(), "row-meta", true));
    gtk_box_append(GTK_BOX(row),
                   makeLabel(item.position.c_str(),
                             item.has_position ? "chat-node-position"
                                               : "row-meta",
                             true));

    GtkWidget* actions = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(actions), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(actions), 3);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(actions), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(actions), 4);
    gtk_flow_box_append(GTK_FLOW_BOX(actions),
                        makeFieldAction("Chat",
                                        item,
                                        G_CALLBACK(onFieldNodeChatClicked),
                                        state));
    gtk_flow_box_append(GTK_FLOW_BOX(actions),
                        makeFieldAction(item.is_contact ? "Added" : "Add",
                                        item,
                                        G_CALLBACK(onChatNodeAddClicked),
                                        state,
                                        !item.is_contact));
    gtk_flow_box_append(GTK_FLOW_BOX(actions),
                        makeFieldAction("Info",
                                        item,
                                        G_CALLBACK(onChatNodeInfoClicked),
                                        state));
    gtk_flow_box_append(
        GTK_FLOW_BOX(actions),
        makeFieldAction(item.is_ignored ? "Unignore" : "Ignore",
                        item,
                        G_CALLBACK(onChatNodeIgnoreClicked),
                        state));
    gtk_flow_box_append(GTK_FLOW_BOX(actions),
                        makeFieldAction("Exchange",
                                        item,
                                        G_CALLBACK(
                                            onChatNodeExchangeUserInfoClicked),
                                        state));
    gtk_flow_box_append(
        GTK_FLOW_BOX(actions),
        makeFieldAction(item.key_verified ? "Trusted" : "Verify key",
                        item,
                        G_CALLBACK(onChatNodeVerifyKeyClicked),
                        state,
                        !item.key_verified));
    gtk_box_append(GTK_BOX(row), actions);
    return row;
}

void appendNodeGroup(GtkUConsoleAppState& state,
                     GtkWidget* panel,
                     const std::vector<ChatNodeInfoItem>& nodes,
                     bool contacts)
{
    bool appended = false;
    for (const auto& item : nodes)
    {
        if (item.is_contact != contacts)
        {
            continue;
        }
        gtk_box_append(GTK_BOX(panel), makeContactNodeRow(state, item));
        appended = true;
    }
    if (!appended)
    {
        gtk_box_append(GTK_BOX(panel),
                       makeLabel(contacts ? "No saved contacts yet."
                                          : "No nearby nodes yet.",
                                 "empty-state"));
    }
}

GtkWidget* launchContactsLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "Contacts & nodes",
        "Desktop node directory with messaging, trust, ignore, and user-info actions.",
        &state.contacts_page_box);
}

void refreshContactsLogic(GtkUConsoleAppState& state,
                          const GtkUConsoleRefreshSnapshot& snapshot)
{
    clearBox(state.contacts_page_box);
    const ChatWorkspaceSnapshot chat = state.chat_model.snapshot(
        kFieldConversationLimit, 0, state.chat_sort_mode);

    GtkWidget* summary = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(summary, "list-summary");
    gtk_box_append(GTK_BOX(summary),
                   makeLabel((std::to_string(snapshot.dashboard.contact_count) +
                              " saved contacts")
                                 .c_str(),
                             "list-summary-item"));
    gtk_box_append(GTK_BOX(summary),
                   makeLabel((std::to_string(snapshot.dashboard.nearby_count) +
                              " nearby")
                                 .c_str(),
                             "list-summary-item"));
    gtk_box_append(GTK_BOX(summary),
                   makeLabel((std::to_string(snapshot.dashboard.ignored_count) +
                              " ignored")
                                 .c_str(),
                             "list-summary-item"));
    gtk_box_append(GTK_BOX(summary),
                   makeLabel((snapshot.dashboard.mesh_protocol + " / " +
                              std::to_string(chat.nodes.size()) + " nodes")
                                 .c_str(),
                             "list-summary-item"));
    gtk_box_append(GTK_BOX(state.contacts_page_box), summary);

    GtkWidget* workbench = makeWorkbench(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* contacts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget* nearby = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(contacts, "field-list");
    gtk_widget_add_css_class(nearby, "field-list");
    gtk_widget_set_hexpand(contacts, TRUE);
    gtk_widget_set_hexpand(nearby, TRUE);
    gtk_box_append(GTK_BOX(contacts),
                   makeLabel("Saved contacts", "pane-heading"));
    gtk_box_append(GTK_BOX(nearby),
                   makeLabel("Nearby and discovered", "pane-heading"));
    appendNodeGroup(state, contacts, chat.nodes, true);
    appendNodeGroup(state, nearby, chat.nodes, false);
    gtk_box_append(GTK_BOX(workbench), contacts);
    gtk_box_append(GTK_BOX(workbench), nearby);
    gtk_box_append(GTK_BOX(state.contacts_page_box), workbench);
}

GtkWidget* makeSatelliteRow(const ::gps::GnssSkyplotSatellite& sat)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(row, "gnss-sat-row");
    gtk_widget_add_css_class(row, sat.used ? "gnss-sat-used"
                                           : "gnss-sat-unused");
    char title[32] = {};
    std::snprintf(title,
                  sizeof(title),
                  "%s %u",
                  ::gps::gnss_system_label(sat.system),
                  static_cast<unsigned>(sat.id));
    char detail[96] = {};
    std::snprintf(detail,
                  sizeof(detail),
                  "%s / SNR %d / elevation %u / azimuth %u",
                  sat.used ? "used" : "in view",
                  static_cast<int>(sat.snr),
                  static_cast<unsigned>(sat.elevation),
                  static_cast<unsigned>(sat.azimuth));
    gtk_box_append(GTK_BOX(row), makeLabel(title, "gnss-sat-title"));
    gtk_box_append(GTK_BOX(row),
                   makeLabel(detail, "gnss-sat-meta", true));
    return row;
}

void onFieldOpenMapClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "map");
}

GtkWidget* launchGpsLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "GPS & sky plot",
        "Live fix, satellite geometry, receiver state, and map handoff.",
        &state.gps_page_box);
}

void refreshGpsLogic(GtkUConsoleAppState& state,
                     const GtkUConsoleRefreshSnapshot& snapshot)
{
    clearBox(state.gps_page_box);

    std::array<::gps::GnssSatInfo, ::gps::kMaxGnssSats> sats{};
    std::size_t sat_count = 0;
    ::gps::GnssStatus status{};
    const bool has_snapshot = ::platform::ui::gps::get_gnss_snapshot(
        sats.data(), sats.size(), &sat_count, &status);
    const auto gps_state = ::platform::ui::gps::get_data();
    state.overview_gnss_view = ::gps::build_gnss_skyplot_view(
        sats.data(),
        sat_count,
        status,
        gps_state,
        has_snapshot,
        kFieldSatelliteLimit);

    GtkWidget* receiver = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_add_css_class(receiver, "receiver-status-row");
    gtk_box_append(GTK_BOX(receiver),
                   makeLabel(snapshot.dashboard.location.state.c_str(),
                             "receiver-status-state"));
    gtk_box_append(GTK_BOX(receiver),
                   makeLabel(snapshot.dashboard.location.coordinates.c_str(),
                             "receiver-status-coordinate"));
    gtk_box_append(GTK_BOX(receiver),
                   makeLabel((std::to_string(
                                  state.overview_gnss_view.status.sats_in_use) +
                              " used / " +
                              std::to_string(
                                  state.overview_gnss_view.status.sats_in_view) +
                              " in view")
                                 .c_str(),
                             "receiver-status-detail"));
    GtkWidget* receiver_detail = makeLabel(
        snapshot.dashboard.location.detail.c_str(), "receiver-status-detail", true);
    gtk_widget_set_hexpand(receiver_detail, TRUE);
    gtk_box_append(GTK_BOX(receiver), receiver_detail);
    gtk_box_append(GTK_BOX(state.gps_page_box), receiver);

    GtkWidget* workbench = makeWorkbench(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* sky_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(sky_panel, "gps-skyplot-workspace");
    gtk_widget_set_hexpand(sky_panel, TRUE);
    gtk_box_append(GTK_BOX(sky_panel),
                   makeLabel("Satellite sky plot", "pane-heading"));
    state.gps_skyplot = gtk_drawing_area_new();
    gtk_widget_add_css_class(state.gps_skyplot, "gnss-skyplot");
    gtk_widget_set_size_request(state.gps_skyplot, 440, 360);
    gtk_widget_set_hexpand(state.gps_skyplot, TRUE);
    gtk_widget_set_vexpand(state.gps_skyplot, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(state.gps_skyplot),
                                   drawOverviewGnssSkyplot,
                                   &state,
                                   nullptr);
    gtk_box_append(GTK_BOX(sky_panel), state.gps_skyplot);

    GtkWidget* satellite_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(satellite_panel, "gps-satellite-list");
    gtk_widget_set_size_request(satellite_panel, 320, -1);
    gtk_box_append(GTK_BOX(satellite_panel),
                   makeLabel("Satellites", "pane-heading"));
    state.gps_satellite_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    if (state.overview_gnss_view.satellites.empty())
    {
        gtk_box_append(GTK_BOX(state.gps_satellite_list),
                       makeLabel("No satellite data.", "empty-state"));
    }
    for (const auto& sat : state.overview_gnss_view.satellites)
    {
        gtk_box_append(GTK_BOX(state.gps_satellite_list),
                       makeSatelliteRow(sat));
    }
    GtkWidget* satellite_scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(satellite_scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(satellite_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(satellite_scroll),
                                  state.gps_satellite_list);
    gtk_box_append(GTK_BOX(satellite_panel), satellite_scroll);

    GtkWidget* open_map = gtk_button_new_with_label("Open map workspace");
    gtk_widget_add_css_class(open_map, "nav-button");
    g_signal_connect(open_map,
                     "clicked",
                     G_CALLBACK(onFieldOpenMapClicked),
                     &state);
    gtk_box_append(GTK_BOX(satellite_panel), open_map);
    gtk_box_append(GTK_BOX(workbench), sky_panel);
    gtk_box_append(GTK_BOX(workbench), satellite_panel);
    gtk_box_append(GTK_BOX(state.gps_page_box), workbench);
}

const ChatConversationItem* firstTeamConversation(
    const ChatWorkspaceSnapshot& snapshot)
{
    const auto it = std::find_if(
        snapshot.conversations.begin(),
        snapshot.conversations.end(),
        [](const ChatConversationItem& item)
        { return item.team; });
    return it == snapshot.conversations.end() ? nullptr : &*it;
}

void onTeamOpenChatClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const auto snapshot = state.chat_model.snapshot(kFieldConversationLimit, 0);
    const auto* conversation = firstTeamConversation(snapshot);
    if (conversation == nullptr)
    {
        state.team_action_status = "No team conversation is available yet.";
        refreshUi(state);
        return;
    }
    state.chat_model.selectConversation(conversation->id);
    showPage(state, "chat");
}

void onTeamSharePositionClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const auto snapshot = state.chat_model.snapshot(kFieldConversationLimit, 0);
    const auto* conversation = firstTeamConversation(snapshot);
    if (conversation == nullptr)
    {
        state.team_action_status = "Join or create a team before sharing.";
    }
    else
    {
        state.chat_model.selectConversation(conversation->id);
        state.team_action_status = state.chat_model.sendCurrentPosition()
                                       ? "Current position queued for the team."
                                       : "Position could not be queued.";
    }
    refreshUi(state);
}

GtkWidget* launchTeamLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "Team operations",
        "Team state, field activity, team chat, and position sharing.",
        &state.team_page_box);
}

void showTeamPage(GtkUConsoleAppState& state)
{
    state.services.setTeamModeActive(true);
}

void hideTeamPage(GtkUConsoleAppState& state)
{
    state.services.setTeamModeActive(false);
}

void refreshTeamLogic(GtkUConsoleAppState& state,
                      const GtkUConsoleRefreshSnapshot& snapshot)
{
    clearBox(state.team_page_box);
    const auto chat = state.chat_model.snapshot(kFieldConversationLimit, 0);
    const bool has_team_chat = firstTeamConversation(chat) != nullptr;

    GtkWidget* summary = makePanel();
    gtk_box_append(GTK_BOX(summary),
                   makeLabel("Team status", "pane-heading"));
    gtk_box_append(GTK_BOX(summary),
                   makeLabel(snapshot.dashboard.team_summary.c_str(),
                             "summary-detail",
                             true));
    if (!state.team_action_status.empty())
    {
        gtk_box_append(GTK_BOX(summary),
                       makeLabel(state.team_action_status.c_str(),
                                 "settings-status",
                                 true));
    }

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* open_chat = gtk_button_new_with_label("Open team chat");
    GtkWidget* share = gtk_button_new_with_label("Share current position");
    gtk_widget_add_css_class(open_chat, "nav-button");
    gtk_widget_add_css_class(share, "nav-button");
    gtk_widget_set_sensitive(open_chat, has_team_chat ? TRUE : FALSE);
    gtk_widget_set_sensitive(share, has_team_chat ? TRUE : FALSE);
    g_signal_connect(open_chat,
                     "clicked",
                     G_CALLBACK(onTeamOpenChatClicked),
                     &state);
    g_signal_connect(share,
                     "clicked",
                     G_CALLBACK(onTeamSharePositionClicked),
                     &state);
    gtk_box_append(GTK_BOX(actions), open_chat);
    gtk_box_append(GTK_BOX(actions), share);
    gtk_box_append(GTK_BOX(summary), actions);
    gtk_box_append(GTK_BOX(state.team_page_box), summary);

    GtkWidget* timeline = makePanel();
    gtk_box_append(GTK_BOX(timeline),
                   makeLabel("Team activity", "pane-heading"));
    if (snapshot.dashboard.team_timeline.empty())
    {
        gtk_box_append(GTK_BOX(timeline),
                       makeLabel("No team activity has been recorded.",
                                 "empty-state"));
    }
    for (const auto& item : snapshot.dashboard.team_timeline)
    {
        gtk_box_append(GTK_BOX(timeline),
                       buildDetailRow(item.title,
                                      item.detail,
                                      item.attention));
    }
    gtk_box_append(GTK_BOX(state.team_page_box), timeline);
}

const char* trackerFormatLabel(std::uint8_t value)
{
    switch (value)
    {
    case 1:
        return "CSV";
    case 2:
        return "Binary";
    case 0:
    default:
        return "GPX";
    }
}

void onTrackerToggleClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    auto& config = state.services.config();
    if (::platform::ui::tracker::is_recording())
    {
        ::platform::ui::tracker::stop_recording();
        config.map_track_enabled = false;
        state.services.applyPositionConfig();
        state.tracker_action_status = "Track recording stopped.";
    }
    else
    {
        config.map_track_enabled = true;
        state.services.applyPositionConfig();
        const bool started = ::platform::ui::tracker::start_recording();
        state.tracker_action_status = started
                                          ? "Track recording started."
                                          : "Track file could not be created.";
        if (!started)
        {
            config.map_track_enabled = false;
            state.services.applyPositionConfig();
        }
    }
    state.services.saveConfig();
    refreshUi(state);
}

GtkWidget* launchTrackerLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "Tracker",
        "Linux-native GPX, CSV, or binary track recording with persistent files.",
        &state.tracker_page_box);
}

void refreshTrackerLogic(GtkUConsoleAppState& state,
                         const GtkUConsoleRefreshSnapshot& snapshot)
{
    clearBox(state.tracker_page_box);
    const auto& config = state.services.config();
    const bool supported = ::platform::ui::tracker::is_supported();
    const bool recording = ::platform::ui::tracker::is_recording();
    std::string current_path{};
    (void)::platform::ui::tracker::current_path(current_path);

    GtkWidget* tracker_status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(tracker_status, "list-summary");
    gtk_box_append(GTK_BOX(tracker_status),
                   makeLabel(recording ? "Recording" : "Stopped",
                             recording ? "tracker-state-active"
                                       : "tracker-state-idle"));
    gtk_box_append(GTK_BOX(tracker_status),
                   makeLabel(trackerFormatLabel(config.map_track_format),
                             "list-summary-item"));
    gtk_box_append(GTK_BOX(tracker_status),
                   makeLabel((std::to_string(config.map_track_interval) +
                              " second interval")
                                 .c_str(),
                             "list-summary-item"));
    gtk_box_append(GTK_BOX(tracker_status),
                   makeLabel(snapshot.dashboard.location.state.c_str(),
                             "list-summary-item"));
    gtk_box_append(GTK_BOX(state.tracker_page_box), tracker_status);

    GtkWidget* controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(controls, "tracker-controls");
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* title = makeLabel("Recording session", "pane-heading");
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(header), title);
    GtkWidget* toggle = gtk_button_new_with_label(
        recording ? "Stop recording" : "Start recording");
    gtk_widget_add_css_class(toggle, "nav-button");
    gtk_widget_set_sensitive(toggle, supported ? TRUE : FALSE);
    g_signal_connect(toggle,
                     "clicked",
                     G_CALLBACK(onTrackerToggleClicked),
                     &state);
    gtk_box_append(GTK_BOX(header), toggle);
    gtk_box_append(GTK_BOX(controls), header);
    gtk_box_append(GTK_BOX(controls),
                   buildDetailRow("Current file",
                                  current_path.empty() ? "No active file"
                                                       : current_path));
    gtk_box_append(GTK_BOX(controls),
                   buildDetailRow("Track directory",
                                  ::platform::ui::tracker::track_dir()));
    if (!state.tracker_action_status.empty())
    {
        gtk_box_append(GTK_BOX(controls),
                       makeLabel(state.tracker_action_status.c_str(),
                                 "settings-status",
                                 true));
    }
    gtk_box_append(GTK_BOX(state.tracker_page_box), controls);

    std::vector<std::string> tracks{};
    (void)::platform::ui::tracker::list_tracks(tracks, 32);
    GtkWidget* history = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(history, "tracker-history");
    gtk_box_append(GTK_BOX(history),
                   makeLabel("Recorded tracks", "pane-heading"));
    if (tracks.empty())
    {
        gtk_box_append(GTK_BOX(history),
                       makeLabel("No track files yet.", "empty-state"));
    }
    for (const auto& track : tracks)
    {
        gtk_box_append(GTK_BOX(history),
                       buildDetailRow(track,
                                      std::string("Stored in ") +
                                          ::platform::ui::tracker::track_dir()));
    }
    gtk_box_append(GTK_BOX(state.tracker_page_box), history);
}

} // namespace

GtkUConsolePageLifecycle makeContactsPageLifecycle()
{
    return {.name = "contacts",
            .title = "Contacts & nodes",
            .onLaunch = launchContactsLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshContactsLogic,
            .onDestroy = nullptr};
}

GtkUConsolePageLifecycle makeGpsPageLifecycle()
{
    return {.name = "gps",
            .title = "GPS & sky plot",
            .onLaunch = launchGpsLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshGpsLogic,
            .onDestroy = nullptr};
}

GtkUConsolePageLifecycle makeTeamPageLifecycle()
{
    return {.name = "team",
            .title = "Team operations",
            .onLaunch = launchTeamLayout,
            .onShow = showTeamPage,
            .onHide = hideTeamPage,
            .onRefresh = refreshTeamLogic,
            .onDestroy = nullptr};
}

GtkUConsolePageLifecycle makeTrackerPageLifecycle()
{
    return {.name = "tracker",
            .title = "Tracker",
            .onLaunch = launchTrackerLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshTrackerLogic,
            .onDestroy = nullptr};
}

} // namespace trailmate::uconsole::gtk
