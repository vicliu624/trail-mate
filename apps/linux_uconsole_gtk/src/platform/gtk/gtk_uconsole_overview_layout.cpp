#include "platform/gtk/gtk_uconsole_layout_spec.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* launchOverviewLayout(GtkUConsoleAppState& state)
{
    GtkWidget* root = makeWorkbench(GTK_ORIENTATION_HORIZONTAL, 8);
    state.hardware_box = nullptr;

    state.overview_location_panel = makePanel();
    gtk_widget_add_css_class(state.overview_location_panel, "pane-primary");
    gtk_widget_set_hexpand(state.overview_location_panel, FALSE);
    gtk_widget_set_size_request(state.overview_location_panel,
                                layout_spec::kOverviewGpsRailWidth,
                                -1);
    gtk_widget_set_vexpand(state.overview_location_panel, TRUE);
    state.overview_location_state = makeLabel("", "summary-title");
    state.overview_location_coordinates = makeLabel("", "row-title");
    state.overview_location_detail = makeLabel("", "summary-detail", true);

    state.overview_location_map = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(state.overview_location_map, "location-map");
    gtk_widget_set_size_request(state.overview_location_map,
                                layout_spec::kOverviewLocationMapWidth,
                                layout_spec::kOverviewLocationMapHeight);
    gtk_widget_set_hexpand(state.overview_location_map, FALSE);
    gtk_widget_set_halign(state.overview_location_map, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(state.overview_location_panel),
                   state.overview_location_map);
    state.overview_location_map_meta = makeLabel("", "row-meta", true);

    state.overview_gnss_skyplot = gtk_drawing_area_new();
    gtk_widget_add_css_class(state.overview_gnss_skyplot, "gnss-skyplot");
    gtk_widget_set_size_request(state.overview_gnss_skyplot,
                                layout_spec::kOverviewSkyplotWidth,
                                layout_spec::kOverviewSkyplotHeight);
    gtk_widget_set_hexpand(state.overview_gnss_skyplot, FALSE);
    gtk_widget_set_halign(state.overview_gnss_skyplot, GTK_ALIGN_CENTER);
    gtk_drawing_area_set_draw_func(
        GTK_DRAWING_AREA(state.overview_gnss_skyplot),
        drawOverviewGnssSkyplot,
        &state,
        nullptr);
    gtk_box_append(GTK_BOX(state.overview_location_panel),
                   state.overview_gnss_skyplot);

    state.overview_satellite_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_add_css_class(state.overview_satellite_list,
                             "gnss-satellite-list");
    GtkWidget* sat_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sat_scroll),
                                  state.overview_satellite_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sat_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(sat_scroll,
                                layout_spec::kOverviewSatelliteListWidth,
                                layout_spec::kOverviewSatelliteListHeight);
    gtk_widget_set_hexpand(sat_scroll, FALSE);
    gtk_widget_set_halign(sat_scroll, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(sat_scroll, TRUE);
    gtk_box_append(GTK_BOX(state.overview_location_panel), sat_scroll);
    gtk_box_append(GTK_BOX(root), state.overview_location_panel);

    GtkWidget* center = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(center, TRUE);
    gtk_widget_set_vexpand(center, TRUE);
    gtk_box_append(GTK_BOX(root), center);

    state.overview_messages_panel = makePanel();
    gtk_widget_add_css_class(state.overview_messages_panel, "overview-summary-panel");
    gtk_widget_set_vexpand(state.overview_messages_panel, FALSE);
    gtk_box_append(GTK_BOX(state.overview_messages_panel),
                   makeLabel("Messages", "overview-panel-title"));
    state.overview_messages_title = makeLabel("", "summary-title");
    state.overview_messages_detail = makeLabel("", "summary-detail", true);
    state.overview_messages_latest = makeLabel("", nullptr, true);
    gtk_box_append(GTK_BOX(state.overview_messages_panel),
                   state.overview_messages_title);
    gtk_box_append(GTK_BOX(state.overview_messages_panel),
                   state.overview_messages_detail);
    gtk_box_append(GTK_BOX(state.overview_messages_panel),
                   state.overview_messages_latest);
    state.team_summary = makeLabel("", "summary-detail", true);
    gtk_box_append(GTK_BOX(state.overview_messages_panel), state.team_summary);
    gtk_box_append(GTK_BOX(center), state.overview_messages_panel);

    GtkWidget* recent_panel = makePanel();
    gtk_widget_add_css_class(recent_panel, "overview-recent-panel");
    gtk_widget_set_vexpand(recent_panel, TRUE);
    gtk_box_append(GTK_BOX(recent_panel),
                   makeLabel("Recent contacts", "overview-panel-title"));
    state.overview_conversations = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_widget_add_css_class(state.overview_conversations, "recent-contact-list");
    gtk_widget_set_vexpand(state.overview_conversations, TRUE);
    GtkWidget* recent_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(recent_scroll),
                                  state.overview_conversations);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(recent_scroll),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(recent_scroll, TRUE);
    gtk_box_append(GTK_BOX(recent_panel), recent_scroll);
    gtk_box_append(GTK_BOX(center), recent_panel);

    GtkWidget* timeline_panel = makePanel();
    gtk_widget_add_css_class(timeline_panel, "overview-timeline-panel");
    gtk_widget_set_hexpand(timeline_panel, FALSE);
    gtk_widget_set_size_request(timeline_panel,
                                layout_spec::kOverviewTimelineRailWidth,
                                -1);
    gtk_widget_set_vexpand(timeline_panel, TRUE);

    GtkWidget* timeline_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    GtkWidget* timeline_title = makeLabel("Activity timeline",
                                          "overview-panel-title");
    gtk_widget_set_hexpand(timeline_title, TRUE);
    gtk_box_append(GTK_BOX(timeline_header), timeline_title);
    state.overview_timeline_filter = gtk_combo_box_text_new();
    gtk_widget_add_css_class(state.overview_timeline_filter,
                             "timeline-filter");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.overview_timeline_filter),
                                   "All");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.overview_timeline_filter),
                                   "Messages");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.overview_timeline_filter),
                                   "NodeInfo");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.overview_timeline_filter),
                                   "Position");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.overview_timeline_filter),
                                   "Telemetry");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.overview_timeline_filter),
                                   "Team");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.overview_timeline_filter),
                                   "System");
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.overview_timeline_filter), 0);
    g_signal_connect(state.overview_timeline_filter,
                     "changed",
                     G_CALLBACK(onOverviewTimelineFilterChanged),
                     &state);
    gtk_box_append(GTK_BOX(timeline_header), state.overview_timeline_filter);
    gtk_box_append(GTK_BOX(timeline_panel), timeline_header);

    GtkWidget* timeline_tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* top_button = gtk_button_new_with_label("Newest");
    GtkWidget* bottom_button = gtk_button_new_with_label("Oldest");
    gtk_widget_add_css_class(top_button, "timeline-jump-button");
    gtk_widget_add_css_class(bottom_button, "timeline-jump-button");
    g_signal_connect(top_button,
                     "clicked",
                     G_CALLBACK(onOverviewTimelineTopClicked),
                     &state);
    g_signal_connect(bottom_button,
                     "clicked",
                     G_CALLBACK(onOverviewTimelineBottomClicked),
                     &state);
    gtk_box_append(GTK_BOX(timeline_tools), top_button);
    gtk_box_append(GTK_BOX(timeline_tools), bottom_button);
    gtk_box_append(GTK_BOX(timeline_panel), timeline_tools);

    state.overview_timeline_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_widget_add_css_class(state.overview_timeline_box, "timeline-list");
    gtk_widget_set_vexpand(state.overview_timeline_box, TRUE);
    state.overview_timeline_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(
        GTK_SCROLLED_WINDOW(state.overview_timeline_scroll),
        state.overview_timeline_box);
    gtk_scrolled_window_set_policy(
        GTK_SCROLLED_WINDOW(state.overview_timeline_scroll),
        GTK_POLICY_NEVER,
        GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(state.overview_timeline_scroll, TRUE);
    gtk_box_append(GTK_BOX(timeline_panel), state.overview_timeline_scroll);
    gtk_box_append(GTK_BOX(root), timeline_panel);
    return root;
}

} // namespace trailmate::uconsole::gtk
