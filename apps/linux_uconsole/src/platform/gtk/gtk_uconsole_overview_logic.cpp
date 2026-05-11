#include "platform/gtk/gtk_uconsole_layout_spec.h"
#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "gps/usecase/gnss_skyplot_presenter.h"
#include "platform/ui/gps_runtime.h"

namespace trailmate::uconsole::gtk
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

void setCairoRgb(cairo_t* cr, double r, double g, double b)
{
    cairo_set_source_rgb(cr, r, g, b);
}

void setSignalColor(cairo_t* cr, ::gps::GnssSignalState state)
{
    switch (state)
    {
    case ::gps::GnssSignalState::Good:
        setCairoRgb(cr, 0.16, 0.47, 0.33);
        break;
    case ::gps::GnssSignalState::Fair:
        setCairoRgb(cr, 0.77, 0.53, 0.14);
        break;
    case ::gps::GnssSignalState::Weak:
        setCairoRgb(cr, 0.75, 0.29, 0.18);
        break;
    case ::gps::GnssSignalState::NotUsed:
        setCairoRgb(cr, 0.45, 0.50, 0.48);
        break;
    case ::gps::GnssSignalState::InView:
    default:
        setCairoRgb(cr, 0.35, 0.43, 0.40);
        break;
    }
}

} // namespace

void refreshLocationMiniMap(GtkUConsoleAppState& state,
                            const MapWorkspaceSnapshot& map_snapshot)
{
    clearBox(state.overview_location_map);
    if (!map_snapshot.has_center)
    {
        gtk_box_append(GTK_BOX(state.overview_location_map),
                       makeLabel("No map center", "row-title"));
        return;
    }

    const MapTileItem* center_tile = nullptr;
    if (map_snapshot.center_tile_index < map_snapshot.tiles.size())
    {
        center_tile = &map_snapshot.tiles[map_snapshot.center_tile_index];
    }

    if (center_tile != nullptr && center_tile->available)
    {
        GtkWidget* picture =
            gtk_picture_new_for_filename(center_tile->path.string().c_str());
        gtk_widget_add_css_class(picture, "location-picture");
        gtk_picture_set_content_fit(GTK_PICTURE(picture),
                                    GTK_CONTENT_FIT_COVER);
        gtk_widget_set_size_request(picture,
                                    layout_spec::kOverviewLocationMapWidth,
                                    layout_spec::kOverviewLocationPictureHeight);
        gtk_widget_set_hexpand(picture, FALSE);
        gtk_widget_set_halign(picture, GTK_ALIGN_CENTER);
        gtk_widget_set_vexpand(picture, TRUE);
        gtk_box_append(GTK_BOX(state.overview_location_map), picture);
    }
    else
    {
        gtk_box_append(GTK_BOX(state.overview_location_map),
                       makeLabel("Tile pending", "row-title"));
        gtk_box_append(GTK_BOX(state.overview_location_map),
                       makeLabel("Center tile is not cached yet.",
                                 "row-meta",
                                 true));
    }

    if (center_tile != nullptr)
    {
        const std::string tile_meta =
            "z" + std::to_string(center_tile->id.z) + " " +
            std::to_string(center_tile->id.x) + "/" +
            std::to_string(center_tile->id.y);
        gtk_box_append(GTK_BOX(state.overview_location_map),
                       makeLabel(tile_meta.c_str(), "row-meta"));
    }
}

void drawOverviewGnssSkyplot(GtkDrawingArea*,
                             cairo_t* cr,
                             int width,
                             int height,
                             gpointer data)
{
    auto* state = static_cast<GtkUConsoleAppState*>(data);
    if (state == nullptr || cr == nullptr || width <= 0 || height <= 0)
    {
        return;
    }

    const double w = static_cast<double>(width);
    const double h = static_cast<double>(height);
    const double cx = w * 0.5;
    const double cy = h * 0.52;
    const double radius = std::max(8.0, std::min(w, h) * 0.42);

    cairo_save(cr);
    setCairoRgb(cr, 0.97, 0.98, 0.96);
    cairo_paint(cr);

    cairo_set_line_width(cr, 1.0);
    setCairoRgb(cr, 0.73, 0.78, 0.73);
    for (int ring = 1; ring <= 3; ++ring)
    {
        cairo_arc(cr, cx, cy, radius * static_cast<double>(ring) / 3.0,
                  0.0, 2.0 * kPi);
        cairo_stroke(cr);
    }
    cairo_move_to(cr, cx - radius, cy);
    cairo_line_to(cr, cx + radius, cy);
    cairo_move_to(cr, cx, cy - radius);
    cairo_line_to(cr, cx, cy + radius);
    cairo_stroke(cr);

    cairo_select_font_face(cr,
                           "Sans",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9.0);

    for (const auto& sat : state->overview_gnss_view.satellites)
    {
        const double az = (static_cast<double>(sat.azimuth) - 90.0) *
                          kPi / 180.0;
        const double normalized_r =
            std::clamp((90.0 - static_cast<double>(sat.elevation)) / 90.0,
                       0.0,
                       1.0);
        const double x = cx + std::cos(az) * radius * normalized_r;
        const double y = cy + std::sin(az) * radius * normalized_r;
        const double dot = sat.used ? 5.5 : 4.5;

        setSignalColor(cr, sat.signal);
        cairo_arc(cr, x, y, dot, 0.0, 2.0 * kPi);
        cairo_fill_preserve(cr);
        setCairoRgb(cr, 0.12, 0.16, 0.14);
        cairo_stroke(cr);

        char label[8] = {};
        std::snprintf(label, sizeof(label), "%u",
                      static_cast<unsigned>(sat.id));
        cairo_text_extents_t extents{};
        cairo_text_extents(cr, label, &extents);
        setCairoRgb(cr, 0.12, 0.16, 0.14);
        cairo_move_to(cr, x - extents.width * 0.5, y - dot - 3.0);
        cairo_show_text(cr, label);
    }

    cairo_set_font_size(cr, 10.0);
    const auto& status = state->overview_gnss_view.status;
    std::string caption =
        std::string(::gps::gnss_fix_label(status.fix)) + " / use " +
        std::to_string(status.sats_in_use) + " / view " +
        std::to_string(status.sats_in_view);
    if (status.hdop > 0.0F)
    {
        char hdop[24] = {};
        std::snprintf(hdop,
                      sizeof(hdop),
                      " / HDOP %.1f",
                      static_cast<double>(status.hdop));
        caption += hdop;
    }
    setCairoRgb(cr, 0.24, 0.29, 0.26);
    cairo_move_to(cr, 8.0, h - 8.0);
    cairo_show_text(cr, caption.c_str());
    cairo_restore(cr);
}

GtkWidget* buildSatelliteRow(const ::gps::GnssSkyplotSatellite& sat)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(row, "gnss-sat-row");
    gtk_widget_add_css_class(row, sat.used ? "gnss-sat-used"
                                           : "gnss-sat-unused");
    switch (sat.signal)
    {
    case ::gps::GnssSignalState::Good:
        gtk_widget_add_css_class(row, "gnss-signal-good");
        break;
    case ::gps::GnssSignalState::Fair:
        gtk_widget_add_css_class(row, "gnss-signal-fair");
        break;
    case ::gps::GnssSignalState::Weak:
        gtk_widget_add_css_class(row, "gnss-signal-weak");
        break;
    case ::gps::GnssSignalState::NotUsed:
    case ::gps::GnssSignalState::InView:
    default:
        gtk_widget_add_css_class(row, "gnss-signal-idle");
        break;
    }

    char id[32] = {};
    std::snprintf(id,
                  sizeof(id),
                  "%s %u",
                  ::gps::gnss_system_label(sat.system),
                  static_cast<unsigned>(sat.id));
    char facts[96] = {};
    std::snprintf(facts,
                  sizeof(facts),
                  "%s / SNR %d / el %u / az %u",
                  sat.used ? "used" : "view",
                  static_cast<int>(sat.snr),
                  static_cast<unsigned>(sat.elevation),
                  static_cast<unsigned>(sat.azimuth));
    GtkWidget* title = makeLabel(id, "gnss-sat-title");
    GtkWidget* meta = makeLabel(facts, "gnss-sat-meta", true);
    gtk_label_set_max_width_chars(GTK_LABEL(meta), 18);
    gtk_box_append(GTK_BOX(row), title);
    gtk_box_append(GTK_BOX(row), meta);
    return row;
}

void refreshOverviewGnss(GtkUConsoleAppState& state)
{
    std::array<::gps::GnssSatInfo, ::gps::kMaxGnssSats> sats{};
    std::size_t sat_count = 0;
    ::gps::GnssStatus status{};
    const bool has_snapshot = ::platform::ui::gps::get_gnss_snapshot(
        sats.data(), sats.size(), &sat_count, &status);
    const auto gps_state = ::platform::ui::gps::get_data();
    state.overview_gnss_view = ::gps::build_gnss_skyplot_view(sats.data(),
                                                              sat_count,
                                                              status,
                                                              gps_state,
                                                              has_snapshot,
                                                              18U);
    if (state.overview_gnss_skyplot != nullptr)
    {
        gtk_widget_queue_draw(state.overview_gnss_skyplot);
    }

    clearBox(state.overview_satellite_list);
    if (!has_snapshot || state.overview_gnss_view.satellites.empty())
    {
        gtk_box_append(GTK_BOX(state.overview_satellite_list),
                       makeLabel("No satellite data.", "empty-state"));
        return;
    }
    for (const auto& sat : state.overview_gnss_view.satellites)
    {
        gtk_box_append(GTK_BOX(state.overview_satellite_list),
                       buildSatelliteRow(sat));
    }
}

const char* timelineFilterKind(int index)
{
    switch (index)
    {
    case 1:
        return "message";
    case 2:
        return "node";
    case 3:
        return "position";
    case 4:
        return "telemetry";
    case 5:
        return "team";
    case 6:
        return "system";
    default:
        return "";
    }
}

bool timelineItemVisible(const OverviewTimelineItem& item, int filter_index)
{
    if (filter_index <= 0)
    {
        return true;
    }
    const std::string filter = timelineFilterKind(filter_index);
    if (filter == "team")
    {
        return item.team || item.kind == "team";
    }
    return item.kind == filter;
}

GtkWidget* makeTimelineBadge(const char* text, const char* css_class)
{
    GtkWidget* badge = makeLabel(text, css_class);
    gtk_label_set_xalign(GTK_LABEL(badge), 0.5F);
    return badge;
}

GtkWidget* buildOverviewTimelineRow(const OverviewTimelineItem& item)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_add_css_class(row, "timeline-row");
    gtk_widget_add_css_class(row,
                             item.team ? "timeline-row-team"
                                       : "timeline-row-mesh");
    gtk_widget_add_css_class(row,
                             item.direct ? "timeline-row-direct"
                                         : "timeline-row-broadcast");
    if (item.outgoing)
    {
        gtk_widget_add_css_class(row, "timeline-row-outgoing");
    }
    const std::string kind_class = "timeline-kind-" + item.kind;
    gtk_widget_add_css_class(row, kind_class.c_str());
    if (item.attention)
    {
        gtk_widget_add_css_class(row, "timeline-row-alert");
    }
    gtk_widget_set_hexpand(row, TRUE);

    GtkWidget* top = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(top), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(top), 3);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(top), 3);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(top), 4);
    gtk_flow_box_append(GTK_FLOW_BOX(top),
                        makeTimelineBadge(item.time_label.c_str(),
                                          "timeline-time"));
    gtk_flow_box_append(GTK_FLOW_BOX(top),
                        makeTimelineBadge(item.team ? "Team" : "Mesh",
                                          item.team
                                              ? "timeline-badge-team"
                                              : "timeline-badge-mesh"));
    gtk_flow_box_append(GTK_FLOW_BOX(top),
                        makeTimelineBadge(item.direct ? "Direct" : "Cast",
                                          item.direct
                                              ? "timeline-badge-direct"
                                              : "timeline-badge-broadcast"));
    if (!item.badge.empty())
    {
        gtk_flow_box_append(GTK_FLOW_BOX(top),
                            makeTimelineBadge(item.badge.c_str(),
                                              "timeline-badge-kind"));
    }
    gtk_box_append(GTK_BOX(row), top);

    gtk_box_append(GTK_BOX(row),
                   makeLabel(item.title.c_str(), "row-title", true));
    gtk_box_append(GTK_BOX(row),
                   makeLabel(item.detail.c_str(), "row-meta", true));
    return row;
}

GtkWidget* buildRecentContactRow(GtkUConsoleAppState& state,
                                 const RecentContactPreview& item)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(row, "recent-contact-row");
    if (item.has_unread)
    {
        gtk_widget_add_css_class(row, "recent-contact-unread");
    }
    gtk_widget_set_hexpand(row, TRUE);

    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* title = makeLabel(item.name.c_str(), "row-title", true);
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(title_row), title);
    gtk_box_append(GTK_BOX(title_row),
                   makeTimelineBadge(item.badge.c_str(),
                                     item.team ? "timeline-badge-team"
                                               : (item.direct
                                                      ? "timeline-badge-direct"
                                                      : "timeline-badge-broadcast")));
    GtkWidget* button = gtk_button_new_with_label("Chat");
    gtk_widget_add_css_class(button, "recent-contact-chat");
    auto* conversation = new ::chat::ConversationId(item.conversation);
    g_object_set_data_full(G_OBJECT(button),
                           "trailmate-conversation",
                           conversation,
                           [](gpointer pointer)
                           {
                               delete static_cast<::chat::ConversationId*>(
                                   pointer);
                           });
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(onOverviewRecentContactClicked),
                     &state);
    gtk_box_append(GTK_BOX(title_row), button);
    gtk_box_append(GTK_BOX(row), title_row);
    gtk_box_append(GTK_BOX(row),
                   makeLabel(item.meta.c_str(), "row-meta", true));
    gtk_box_append(GTK_BOX(row),
                   makeLabel(item.detail.c_str(), "summary-detail", true));
    return row;
}

static void refreshOverview(GtkUConsoleAppState& state,
                            const UConsoleDashboardSnapshot& snapshot,
                            const MapWorkspaceSnapshot& map_snapshot)
{
    setAttentionClass(state.overview_location_panel,
                      snapshot.location.attention);
    setAttentionClass(state.overview_messages_panel,
                      snapshot.messages.attention);
    setLabel(state.overview_location_state, snapshot.location.state);
    setLabel(state.overview_location_coordinates,
             snapshot.location.coordinates);
    setLabel(state.overview_location_detail, snapshot.location.detail);
    const std::string map_meta =
        map_snapshot.source_label + " / z" + std::to_string(map_snapshot.zoom) +
        " / " + std::to_string(map_snapshot.cache_stats.cached_tiles) +
        " cached tiles";
    setLabel(state.overview_location_map_meta,
             map_meta.empty() ? snapshot.location.map_meta : map_meta);
    refreshLocationMiniMap(state, map_snapshot);
    refreshOverviewGnss(state);

    setLabel(state.overview_messages_title, snapshot.messages.title);
    setLabel(state.overview_messages_detail, snapshot.messages.detail);
    setLabel(state.overview_messages_latest, snapshot.messages.latest);
    setLabel(state.team_summary, snapshot.team_summary);

    clearBox(state.overview_conversations);
    if (snapshot.recent_contacts.empty())
    {
        gtk_box_append(GTK_BOX(state.overview_conversations),
                       makeLabel("No recent contacts yet.", "empty-state"));
    }
    for (const auto& item : snapshot.recent_contacts)
    {
        gtk_box_append(GTK_BOX(state.overview_conversations),
                       buildRecentContactRow(state, item));
    }

    clearBox(state.overview_timeline_box);
    std::size_t visible_count = 0;
    for (const auto& item : snapshot.timeline)
    {
        if (!timelineItemVisible(item, state.overview_timeline_filter_index))
        {
            continue;
        }
        gtk_box_append(GTK_BOX(state.overview_timeline_box),
                       buildOverviewTimelineRow(item));
        ++visible_count;
    }
    if (visible_count == 0U)
    {
        gtk_box_append(GTK_BOX(state.overview_timeline_box),
                       makeLabel("No activity matches this filter.",
                                 "empty-state"));
    }
}

static void refreshCapabilities(GtkUConsoleAppState& state,
                                const UConsoleDashboardSnapshot& snapshot)
{
    if (state.capability_box == nullptr)
    {
        return;
    }
    clearBox(state.capability_box);
    for (const auto& line : snapshot.capability_lines)
    {
        gtk_box_append(GTK_BOX(state.capability_box),
                       makeLabel(line.c_str(), "row-meta", true));
    }
}

void onOverviewRecentContactClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    auto* conversation = static_cast<::chat::ConversationId*>(
        g_object_get_data(G_OBJECT(button), "trailmate-conversation"));
    if (conversation == nullptr)
    {
        return;
    }
    state.chat_model.selectConversation(*conversation);
    showPage(state, "chat");
}

void onOverviewTimelineFilterChanged(GtkComboBox* combo, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.overview_timeline_filter_index = gtk_combo_box_get_active(combo);
    refreshUi(state);
}

void onOverviewTimelineTopClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.overview_timeline_scroll == nullptr)
    {
        return;
    }
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(state.overview_timeline_scroll));
    if (adjustment != nullptr)
    {
        gtk_adjustment_set_value(adjustment,
                                 gtk_adjustment_get_lower(adjustment));
    }
}

void onOverviewTimelineBottomClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    if (state.overview_timeline_scroll == nullptr)
    {
        return;
    }
    GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(
        GTK_SCROLLED_WINDOW(state.overview_timeline_scroll));
    if (adjustment != nullptr)
    {
        const double bottom = std::max(
            gtk_adjustment_get_lower(adjustment),
            gtk_adjustment_get_upper(adjustment) -
                gtk_adjustment_get_page_size(adjustment));
        gtk_adjustment_set_value(adjustment, bottom);
    }
}

void refreshOverviewLogic(GtkUConsoleAppState& state,
                          const GtkUConsoleRefreshSnapshot& snapshot)
{
    refreshOverview(state, snapshot.dashboard, snapshot.map);
    refreshCapabilities(state, snapshot.dashboard);
}

GtkUConsolePageLifecycle makeOverviewPageLifecycle()
{
    return {.name = "overview",
            .title = "Overview",
            .onLaunch = launchOverviewLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshOverviewLogic,
            .onDestroy = nullptr};
}

} // namespace trailmate::uconsole::gtk
