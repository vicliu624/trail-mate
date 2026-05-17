#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace trailmate::uconsole::gtk
{

int sortModeIndex(ChatThreadSortMode mode)
{
    switch (mode)
    {
    case ChatThreadSortMode::Hops:
        return 1;
    case ChatThreadSortMode::Distance:
        return 2;
    case ChatThreadSortMode::LastSeen:
        return 3;
    case ChatThreadSortMode::Recent:
    default:
        return 0;
    }
}

ChatThreadSortMode sortModeFromIndex(int index)
{
    switch (index)
    {
    case 1:
        return ChatThreadSortMode::Hops;
    case 2:
        return ChatThreadSortMode::Distance;
    case 3:
        return ChatThreadSortMode::LastSeen;
    case 0:
    default:
        return ChatThreadSortMode::Recent;
    }
}

constexpr int kNodeInfoMapWidth = 420;
constexpr int kNodeInfoMapHeight = 292;

struct NodeInfoMapLine
{
    double node_x = 0.0;
    double node_y = 0.0;
    double self_x = 0.0;
    double self_y = 0.0;
};

std::string formatGtkNodeId(::chat::NodeId node_id)
{
    char buffer[16] = {};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "!%08lX",
                  static_cast<unsigned long>(node_id));
    return buffer;
}

std::string formatNodeInfoCoordinate(const char* prefix, double value)
{
    char buffer[40] = {};
    std::snprintf(buffer, sizeof(buffer), "%s %.5f", prefix, value);
    return buffer;
}

std::string formatNodeInfoDistance(double meters)
{
    if (!std::isfinite(meters) || meters < 0.0)
    {
        return "-";
    }
    char buffer[32] = {};
    if (meters < 1000.0)
    {
        std::snprintf(buffer, sizeof(buffer), "%.0f m", meters);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%.1f km", meters / 1000.0);
    }
    return buffer;
}

const char* compassRose(double bearing)
{
    static constexpr const char* kNames[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    if (!std::isfinite(bearing))
    {
        return "-";
    }
    const int index =
        static_cast<int>(std::lround(std::fmod(bearing + 360.0, 360.0) /
                                     45.0)) %
        8;
    return kNames[index];
}

int nodeInfoZoomFor(const ChatNodeDetailSnapshot& detail)
{
    if (!detail.has_self_position || !std::isfinite(detail.distance_m))
    {
        return 12;
    }
    if (detail.distance_m > 500000.0) return 4;
    if (detail.distance_m > 100000.0) return 6;
    if (detail.distance_m > 20000.0) return 8;
    if (detail.distance_m > 5000.0) return 10;
    if (detail.distance_m > 1000.0) return 12;
    return 14;
}

std::string detailRowValue(const ChatNodeDetailSnapshot& detail,
                           const char* label)
{
    for (const auto& section : detail.sections)
    {
        for (const auto& row : section.rows)
        {
            if (row.label == label)
            {
                return row.value;
            }
        }
    }
    return {};
}

void drawNodeInfoLine(GtkDrawingArea*,
                      cairo_t* cr,
                      int width,
                      int height,
                      gpointer data)
{
    const auto* line = static_cast<const NodeInfoMapLine*>(data);
    if (line == nullptr || cr == nullptr)
    {
        return;
    }
    cairo_set_source_rgba(cr, 0.10, 0.34, 0.30, 0.78);
    cairo_set_line_width(cr, 2.0);
    cairo_move_to(cr,
                  line->node_x * static_cast<double>(width),
                  line->node_y * static_cast<double>(height));
    cairo_line_to(cr,
                  line->self_x * static_cast<double>(width),
                  line->self_y * static_cast<double>(height));
    cairo_stroke(cr);
}

double mapLongitudeToWorldPxForChat(double lon, int zoom)
{
    constexpr double kTileSizePx = 256.0;
    const double tiles = static_cast<double>(1U << zoom);
    return ((lon + 180.0) / 360.0) * tiles * kTileSizePx;
}

double mapLatitudeToWorldPxForChat(double lat, int zoom)
{
    constexpr double kTileSizePx = 256.0;
    constexpr double kMaxMercatorLat = 85.05112878;
    const double clamped_lat =
        std::clamp(lat, -kMaxMercatorLat, kMaxMercatorLat);
    const double lat_rad = clamped_lat * 3.14159265358979323846 / 180.0;
    const double tiles = static_cast<double>(1U << zoom);
    const double mercator =
        std::log(std::tan(lat_rad) + (1.0 / std::cos(lat_rad)));
    return ((1.0 - mercator / 3.14159265358979323846) / 2.0) * tiles *
           kTileSizePx;
}

bool projectNodeInfoMapPoint(const MapWorkspaceSnapshot& snapshot,
                             double lat,
                             double lon,
                             double& out_x_fraction,
                             double& out_y_fraction)
{
    if (!snapshot.has_center || snapshot.tiles.empty() ||
        !std::isfinite(lat) || !std::isfinite(lon))
    {
        return false;
    }

    constexpr double kTileSizePx = 256.0;
    const auto top_left = snapshot.tiles.front().id;
    const double map_width_px =
        static_cast<double>(std::max<std::size_t>(1U, snapshot.columns)) *
        kTileSizePx;
    const double map_height_px =
        static_cast<double>(std::max<std::size_t>(1U, snapshot.rows)) *
        kTileSizePx;
    const double world_width_px =
        static_cast<double>(1U << snapshot.zoom) * kTileSizePx;
    const double left_px = static_cast<double>(top_left.x) * kTileSizePx;
    const double top_px = static_cast<double>(top_left.y) * kTileSizePx;

    double x = mapLongitudeToWorldPxForChat(lon, snapshot.zoom) - left_px;
    if (x < 0.0)
    {
        x += world_width_px;
    }
    if (x > map_width_px && (x - world_width_px) >= 0.0)
    {
        x -= world_width_px;
    }
    const double y = mapLatitudeToWorldPxForChat(lat, snapshot.zoom) - top_px;
    if (x < 0.0 || x > map_width_px || y < 0.0 || y > map_height_px)
    {
        return false;
    }
    out_x_fraction = map_width_px > 0.0 ? x / map_width_px : 0.0;
    out_y_fraction = map_height_px > 0.0 ? y / map_height_px : 0.0;
    return true;
}

void onConversationActivated(GtkListBox*,
                             GtkListBoxRow* row,
                             gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const guint index =
        GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "trailmate-index"));
    if (state.chat_model.selectConversationAt(index,
                                              kConversationLimit,
                                              state.chat_sort_mode))
    {
        refreshUi(state);
    }
}

void onConversationButtonClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const guint index = GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(button), "trailmate-index"));
    if (state.chat_model.selectConversationAt(index,
                                              kConversationLimit,
                                              state.chat_sort_mode))
    {
        refreshUi(state);
    }
}

void onChatSortChanged(GtkComboBox* combo, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_sort_mode = sortModeFromIndex(gtk_combo_box_get_active(combo));
    refreshUi(state);
}

void onChatGroupExpandedChanged(GObject* object, GParamSpec*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    const char* group =
        static_cast<const char*>(g_object_get_data(object, "trailmate-group"));
    if (group == nullptr)
    {
        return;
    }
    state.chat_group_expanded[group] =
        gtk_expander_get_expanded(GTK_EXPANDER(object));
}

void submitChatComposer(GtkUConsoleAppState& state)
{
    const char* text = gtk_editable_get_text(GTK_EDITABLE(state.chat_entry));
    if (state.chat_model.sendText(text ? text : ""))
    {
        gtk_editable_set_text(GTK_EDITABLE(state.chat_entry), "");
    }
    refreshUi(state);
}

gboolean scrollChatTranscriptToBottom(gpointer data)
{
    if (data == nullptr)
    {
        return G_SOURCE_REMOVE;
    }
    auto* scrolled = GTK_SCROLLED_WINDOW(data);
    GtkAdjustment* adjustment =
        gtk_scrolled_window_get_vadjustment(scrolled);
    if (adjustment != nullptr)
    {
        gtk_adjustment_set_value(adjustment,
                                 gtk_adjustment_get_upper(adjustment));
    }
    return G_SOURCE_REMOVE;
}

void onSendClicked(GtkButton*, gpointer data)
{
    submitChatComposer(*static_cast<GtkUConsoleAppState*>(data));
}

void onChatEntryActivate(GtkEntry*, gpointer data)
{
    submitChatComposer(*static_cast<GtkUConsoleAppState*>(data));
}

void onChatAddContactClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.addActivePeerAsContact();
    refreshUi(state);
}

void onChatRequestNodeInfoClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.requestActiveNodeInfo();
    refreshUi(state);
}

void onChatSendPositionClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.sendCurrentPosition();
    refreshUi(state);
}

void onChatSendPoiClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.sendCurrentPoi();
    refreshUi(state);
}

::chat::NodeId nodeIdFromButton(GtkButton* button)
{
    return static_cast<::chat::NodeId>(GPOINTER_TO_UINT(
        g_object_get_data(G_OBJECT(button), "trailmate-node-id")));
}

GtkWidget* buildNodeDetailRow(const ChatNodeDetailRow& row)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(box, "node-info-row");

    GtkWidget* label = makeLabel(row.label.c_str(), "node-info-key", true);
    gtk_widget_set_size_request(label, 96, -1);
    gtk_widget_set_hexpand(label, FALSE);
    gtk_box_append(GTK_BOX(box), label);

    GtkWidget* value =
        makeLabel(row.value.c_str(),
                  row.attention ? "node-info-value-attention"
                                : "node-info-value",
                  true);
    gtk_widget_set_hexpand(value, TRUE);
    gtk_box_append(GTK_BOX(box), value);
    return box;
}

GtkWidget* buildNodeDetailSection(const ChatNodeDetailSection& section)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_add_css_class(box, "node-info-section");
    gtk_box_append(GTK_BOX(box),
                   makeLabel(section.title.c_str(), "node-info-section-title"));
    for (const auto& row : section.rows)
    {
        gtk_box_append(GTK_BOX(box), buildNodeDetailRow(row));
    }
    return box;
}

GtkWidget* buildNodeInfoTileCell(const MapTileItem& item)
{
    GtkWidget* cell = gtk_overlay_new();
    gtk_widget_add_css_class(cell, "node-info-map-tile");
    gtk_widget_set_hexpand(cell, TRUE);
    gtk_widget_set_vexpand(cell, TRUE);
    gtk_widget_set_can_target(cell, FALSE);

    if (item.available)
    {
        GtkWidget* picture =
            gtk_picture_new_for_filename(item.path.string().c_str());
        gtk_picture_set_content_fit(GTK_PICTURE(picture),
                                    GTK_CONTENT_FIT_FILL);
        gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
        gtk_widget_set_hexpand(picture, TRUE);
        gtk_widget_set_vexpand(picture, TRUE);
        gtk_widget_set_can_target(picture, FALSE);
        gtk_overlay_set_child(GTK_OVERLAY(cell), picture);
    }
    else
    {
        GtkWidget* pending = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_add_css_class(pending, "node-info-map-tile-pending");
        gtk_widget_set_hexpand(pending, TRUE);
        gtk_widget_set_vexpand(pending, TRUE);
        gtk_widget_set_can_target(pending, FALSE);
        gtk_overlay_set_child(GTK_OVERLAY(cell), pending);
    }
    return cell;
}

GtkWidget* buildNodeInfoMapGrid(const MapWorkspaceSnapshot& snapshot)
{
    GtkWidget* grid = gtk_grid_new();
    gtk_widget_add_css_class(grid, "node-info-map-grid");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 0);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 0);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_widget_set_hexpand(grid, TRUE);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_widget_set_can_target(grid, FALSE);

    for (std::size_t index = 0; index < snapshot.tiles.size(); ++index)
    {
        const auto columns = std::max<std::size_t>(1U, snapshot.columns);
        const int col = static_cast<int>(index % columns);
        const int row = static_cast<int>(index / columns);
        gtk_grid_attach(GTK_GRID(grid),
                        buildNodeInfoTileCell(snapshot.tiles[index]),
                        col,
                        row,
                        1,
                        1);
    }
    return grid;
}

GtkWidget* makeNodeInfoMarker(const char* text, const char* css_class)
{
    GtkWidget* marker = makeLabel(text, css_class);
    gtk_label_set_xalign(GTK_LABEL(marker), 0.5F);
    gtk_widget_set_can_target(marker, FALSE);
    return marker;
}

GtkWidget* buildNodeInfoOverlayPanel(const ChatNodeDetailSnapshot& detail)
{
    GtkWidget* panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_add_css_class(panel, "node-info-map-panel");
    const std::string protocol = detailRowValue(detail, "Protocol");
    const std::string rssi = detailRowValue(detail, "RSSI");
    const std::string snr = detailRowValue(detail, "SNR");
    const std::string seen = detailRowValue(detail, "Last seen");
    if (!protocol.empty())
    {
        gtk_box_append(GTK_BOX(panel),
                       makeLabel(protocol.c_str(), "node-info-map-protocol"));
    }
    if (!rssi.empty())
    {
        gtk_box_append(GTK_BOX(panel),
                       makeLabel(rssi.c_str(), "node-info-map-rssi"));
    }
    if (!snr.empty())
    {
        gtk_box_append(GTK_BOX(panel),
                       makeLabel(snr.c_str(), "node-info-map-snr"));
    }
    if (!seen.empty())
    {
        gtk_box_append(GTK_BOX(panel),
                       makeLabel(seen.c_str(), "node-info-map-seen"));
    }
    return panel;
}

GtkWidget* buildNodeInfoMapStage(GtkUConsoleAppState& state,
                                 const ChatNodeDetailSnapshot& detail)
{
    GtkWidget* stage = gtk_overlay_new();
    gtk_widget_add_css_class(stage, "node-info-map-stage");
    gtk_widget_set_size_request(stage, kNodeInfoMapWidth, kNodeInfoMapHeight);
    gtk_widget_set_hexpand(stage, TRUE);
    gtk_widget_set_vexpand(stage, TRUE);
    gtk_widget_set_overflow(stage, GTK_OVERFLOW_HIDDEN);

    if (!detail.has_position)
    {
        GtkWidget* empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(empty, "node-info-map-empty");
        gtk_widget_set_valign(empty, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(empty, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(empty),
                       makeLabel("No position available", "row-title"));
        gtk_box_append(GTK_BOX(empty),
                       makeLabel(formatGtkNodeId(detail.node_id).c_str(),
                                 "row-meta"));
        gtk_overlay_set_child(GTK_OVERLAY(stage), empty);
        return stage;
    }

    const int zoom = nodeInfoZoomFor(detail);
    MapWorkspaceSnapshot snapshot =
        state.map_model.snapshotAround(detail.lat, detail.lon, zoom, 0, 0);
    if (!snapshot.tiles.empty() && !snapshot.tiles.front().available)
    {
        static_cast<void>(state.map_model.ensureTile(snapshot.tiles.front().id));
        snapshot =
            state.map_model.snapshotAround(detail.lat, detail.lon, zoom, 0, 0);
    }
    gtk_overlay_set_child(GTK_OVERLAY(stage), buildNodeInfoMapGrid(snapshot));

    double node_x = 0.5;
    double node_y = 0.5;
    static_cast<void>(projectNodeInfoMapPoint(snapshot,
                                              detail.lat,
                                              detail.lon,
                                              node_x,
                                              node_y));

    double self_x = 0.0;
    double self_y = 0.0;
    const bool self_visible =
        detail.has_self_position &&
        projectNodeInfoMapPoint(snapshot,
                                detail.self_lat,
                                detail.self_lon,
                                self_x,
                                self_y);
    if (self_visible)
    {
        auto* line = g_new0(NodeInfoMapLine, 1);
        line->node_x = node_x;
        line->node_y = node_y;
        line->self_x = self_x;
        line->self_y = self_y;
        GtkWidget* drawing = gtk_drawing_area_new();
        gtk_widget_set_can_target(drawing, FALSE);
        gtk_widget_set_hexpand(drawing, TRUE);
        gtk_widget_set_vexpand(drawing, TRUE);
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(drawing),
                                       drawNodeInfoLine,
                                       line,
                                       g_free);
        gtk_overlay_add_overlay(GTK_OVERLAY(stage), drawing);
    }

    GtkWidget* layer = gtk_fixed_new();
    gtk_widget_set_hexpand(layer, TRUE);
    gtk_widget_set_vexpand(layer, TRUE);
    gtk_widget_set_can_target(layer, FALSE);
    gtk_overlay_add_overlay(GTK_OVERLAY(stage), layer);

    gtk_fixed_put(GTK_FIXED(layer),
                  makeNodeInfoMarker("NODE", "node-info-marker-node"),
                  std::clamp(node_x * kNodeInfoMapWidth - 23.0,
                             4.0,
                             static_cast<double>(kNodeInfoMapWidth - 56)),
                  std::clamp(node_y * kNodeInfoMapHeight - 11.0,
                             4.0,
                             static_cast<double>(kNodeInfoMapHeight - 26)));

    if (self_visible)
    {
        gtk_fixed_put(GTK_FIXED(layer),
                      makeNodeInfoMarker("ME", "node-info-marker-self"),
                      std::clamp(self_x * kNodeInfoMapWidth - 15.0,
                                 4.0,
                                 static_cast<double>(kNodeInfoMapWidth - 40)),
                      std::clamp(self_y * kNodeInfoMapHeight - 10.0,
                                 4.0,
                                 static_cast<double>(kNodeInfoMapHeight - 24)));
    }

    gtk_fixed_put(GTK_FIXED(layer),
                  makeLabel(formatGtkNodeId(detail.node_id).c_str(),
                            "node-info-map-id"),
                  8.0,
                  8.0);
    gtk_fixed_put(GTK_FIXED(layer),
                  buildNodeInfoOverlayPanel(detail),
                  static_cast<double>(kNodeInfoMapWidth - 128),
                  8.0);
    gtk_fixed_put(GTK_FIXED(layer),
                  makeLabel(formatNodeInfoCoordinate("LON", detail.lon).c_str(),
                            "node-info-map-lon"),
                  8.0,
                  static_cast<double>(kNodeInfoMapHeight - 44));
    gtk_fixed_put(GTK_FIXED(layer),
                  makeLabel(formatNodeInfoCoordinate("LAT", detail.lat).c_str(),
                            "node-info-map-lat"),
                  8.0,
                  static_cast<double>(kNodeInfoMapHeight - 24));

    if (detail.has_self_position)
    {
        std::string distance = formatNodeInfoDistance(detail.distance_m);
        distance += " / ";
        distance += compassRose(detail.bearing_deg);
        const double label_x =
            self_visible ? ((node_x + self_x) * 0.5 * kNodeInfoMapWidth - 44.0)
                         : 136.0;
        const double label_y =
            self_visible ? ((node_y + self_y) * 0.5 * kNodeInfoMapHeight - 14.0)
                         : static_cast<double>(kNodeInfoMapHeight - 46);
        gtk_fixed_put(GTK_FIXED(layer),
                      makeLabel(distance.c_str(), "node-info-distance"),
                      std::clamp(label_x,
                                 6.0,
                                 static_cast<double>(kNodeInfoMapWidth - 110)),
                      std::clamp(label_y,
                                 6.0,
                                 static_cast<double>(kNodeInfoMapHeight - 30)));
    }

    return stage;
}

void showChatNodeInfoDialog(GtkUConsoleAppState& state, ::chat::NodeId node_id)
{
    const ChatNodeDetailSnapshot detail = state.chat_model.nodeDetails(node_id);

    GtkWidget* dialog = gtk_window_new();
    gtk_widget_add_css_class(dialog, "node-info-dialog");
    gtk_window_set_title(GTK_WINDOW(dialog), detail.title.c_str());
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 456, 392);
    if (state.window != nullptr)
    {
        gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                     GTK_WINDOW(state.window));
    }

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(root, "node-info-dialog-body");

    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_append(GTK_BOX(header),
                   makeLabel(detail.title.c_str(), "node-info-title", true));
    gtk_box_append(GTK_BOX(header),
                   makeLabel(detail.subtitle.c_str(), "row-meta", true));
    gtk_box_append(GTK_BOX(root), header);

    gtk_box_append(GTK_BOX(root), buildNodeInfoMapStage(state, detail));

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(actions, GTK_ALIGN_END);
    GtkWidget* close = gtk_button_new_with_label("Close");
    gtk_widget_add_css_class(close, "chat-action-button");
    g_signal_connect_swapped(close,
                             "clicked",
                             G_CALLBACK(gtk_window_destroy),
                             dialog);
    gtk_box_append(GTK_BOX(actions), close);
    gtk_box_append(GTK_BOX(root), actions);

    gtk_window_set_child(GTK_WINDOW(dialog), root);
    gtk_window_present(GTK_WINDOW(dialog));
}

GtkWidget* makeNodeActionButton(GtkUConsoleAppState& state,
                                const char* label,
                                const ChatNodeInfoItem& item,
                                GCallback callback,
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

void onChatNodeChatClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.selectNodeConversation(nodeIdFromButton(button));
    refreshUi(state);
}

void onChatNodeAddClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.addNodeAsContact(nodeIdFromButton(button));
    refreshUi(state);
}

void onChatNodeInfoClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    showChatNodeInfoDialog(state, nodeIdFromButton(button));
}

void onChatNodeIgnoreClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.toggleNodeIgnored(nodeIdFromButton(button));
    refreshUi(state);
}

void onChatNodeExchangeUserInfoClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.exchangeUserInfo(nodeIdFromButton(button));
    refreshUi(state);
}

void onChatNodeVerifyKeyClicked(GtkButton* button, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.chat_model.verifyNodeKey(nodeIdFromButton(button));
    refreshUi(state);
}

GtkWidget* buildChatNodeInfoCard(GtkUConsoleAppState& state,
                                 const ChatNodeInfoItem& item)
{
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(card, "chat-node-card");
    if (item.via_mqtt)
    {
        gtk_widget_add_css_class(card, "chat-node-mqtt");
    }

    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* title = makeLabel(item.title.c_str(), "row-title");
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(title_row), title);
    gtk_box_append(GTK_BOX(title_row),
                   makeLabel(item.via_mqtt ? "MQTT" : "LoRa", "mini-chip"));
    gtk_box_append(GTK_BOX(card), title_row);
    gtk_box_append(GTK_BOX(card),
                   makeLabel(item.subtitle.c_str(), "row-meta", true));
    gtk_box_append(GTK_BOX(card),
                   makeLabel(item.status.c_str(), "row-meta", true));
    gtk_box_append(GTK_BOX(card),
                   makeLabel(item.signal.c_str(), "row-meta", true));
    gtk_box_append(GTK_BOX(card),
                   makeLabel(item.position.c_str(),
                             item.has_position ? "chat-node-position"
                                               : "row-meta",
                             true));
    GtkWidget* actions = gtk_flow_box_new();
    gtk_widget_add_css_class(actions, "chat-node-actions");
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(actions), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(actions), 3);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(actions), 4);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(actions), 4);
    gtk_flow_box_append(
        GTK_FLOW_BOX(actions),
        makeNodeActionButton(state,
                             "Chat",
                             item,
                             G_CALLBACK(onChatNodeChatClicked)));
    gtk_flow_box_append(GTK_FLOW_BOX(actions),
                        makeNodeActionButton(state,
                                             item.is_contact ? "Added" : "Add",
                                             item,
                                             G_CALLBACK(onChatNodeAddClicked),
                                             !item.is_contact));
    gtk_flow_box_append(
        GTK_FLOW_BOX(actions),
        makeNodeActionButton(state,
                             "Info",
                             item,
                             G_CALLBACK(onChatNodeInfoClicked)));
    gtk_flow_box_append(GTK_FLOW_BOX(actions),
                        makeNodeActionButton(
                            state,
                            item.is_ignored ? "Unignore" : "Ignore",
                            item,
                            G_CALLBACK(onChatNodeIgnoreClicked)));
    gtk_flow_box_append(GTK_FLOW_BOX(actions),
                        makeNodeActionButton(
                            state,
                            "Exchange",
                            item,
                            G_CALLBACK(onChatNodeExchangeUserInfoClicked)));
    gtk_flow_box_append(GTK_FLOW_BOX(actions),
                        makeNodeActionButton(
                            state,
                            item.key_verified ? "Trusted" : "Key",
                            item,
                            G_CALLBACK(onChatNodeVerifyKeyClicked),
                            !item.key_verified));
    gtk_box_append(GTK_BOX(card), actions);
    return card;
}

GtkWidget* makeConversationButton(const ChatConversationItem& item,
                                  std::size_t index,
                                  GtkUConsoleAppState& state)
{
    GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(row_box, "chat-thread-row");
    if (item.active)
    {
        gtk_widget_add_css_class(row_box, "chat-thread-active");
    }
    if (item.team)
    {
        gtk_widget_add_css_class(row_box, "chat-thread-team");
    }
    if (item.broadcast)
    {
        gtk_widget_add_css_class(row_box, "chat-thread-broadcast");
    }

    GtkWidget* title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* title = makeLabel(item.title.c_str(), "chat-thread-title");
    gtk_widget_set_hexpand(title, TRUE);
    gtk_box_append(GTK_BOX(title_row), title);
    if (item.unread > 0)
    {
        char unread[16] = {};
        std::snprintf(unread, sizeof(unread), "%d", item.unread);
        GtkWidget* unread_label = makeLabel(unread, "chat-thread-unread");
        gtk_label_set_xalign(GTK_LABEL(unread_label), 0.5F);
        gtk_box_append(GTK_BOX(title_row), unread_label);
    }
    gtk_box_append(GTK_BOX(row_box), title_row);
    if (!item.preview.empty())
    {
        gtk_box_append(GTK_BOX(row_box),
                       makeLabel(item.preview.c_str(),
                                 "chat-thread-preview",
                                 true));
    }
    if (!item.unread_source.empty())
    {
        gtk_box_append(GTK_BOX(row_box),
                       makeLabel(item.unread_source.c_str(),
                                 "chat-thread-unread-source",
                                 true));
    }
    gtk_box_append(GTK_BOX(row_box),
                   makeLabel(item.facts.c_str(), "chat-thread-facts", true));
    gtk_box_append(GTK_BOX(row_box),
                   makeLabel(item.meta.c_str(), "row-meta", true));

    GtkWidget* button = gtk_button_new();
    gtk_widget_add_css_class(button, "chat-thread-button");
    gtk_button_set_child(GTK_BUTTON(button), row_box);
    g_object_set_data(G_OBJECT(button),
                      "trailmate-index",
                      GUINT_TO_POINTER(static_cast<guint>(index)));
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(onConversationButtonClicked),
                     &state);
    return button;
}

std::vector<std::pair<std::string, std::string>> chatGroupOrder()
{
    return {{"Nearby", "Nearby people"},
            {"Contacts", "Contacts"},
            {"Broadcast", "Broadcast"},
            {"Team", "Team"}};
}

void refreshConversationGroups(GtkUConsoleAppState& state,
                               const ChatWorkspaceSnapshot& snapshot)
{
    clearBox(state.chat_conversation_list);
    if (snapshot.conversations.empty())
    {
        GtkWidget* empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(empty, "empty-state");
        gtk_box_append(GTK_BOX(empty), makeLabel("No threads", "row-title"));
        gtk_box_append(GTK_BOX(empty),
                       makeLabel("No messages are stored locally.",
                                 "row-meta"));
        gtk_box_append(GTK_BOX(state.chat_conversation_list), empty);
        return;
    }

    for (const auto& group : chatGroupOrder())
    {
        std::vector<std::size_t> indexes{};
        for (std::size_t index = 0; index < snapshot.conversations.size();
             ++index)
        {
            if (snapshot.conversations[index].group == group.first)
            {
                indexes.push_back(index);
            }
        }
        if (indexes.empty())
        {
            continue;
        }

        const std::string label =
            group.second + " (" + std::to_string(indexes.size()) + ")";
        GtkWidget* expander = gtk_expander_new(label.c_str());
        gtk_widget_add_css_class(expander, "chat-group");
        const auto expanded_it = state.chat_group_expanded.find(group.first);
        gtk_expander_set_expanded(
            GTK_EXPANDER(expander),
            expanded_it == state.chat_group_expanded.end()
                ? TRUE
                : (expanded_it->second ? TRUE : FALSE));
        g_object_set_data_full(G_OBJECT(expander),
                               "trailmate-group",
                               g_strdup(group.first.c_str()),
                               g_free);
        g_signal_connect(expander,
                         "notify::expanded",
                         G_CALLBACK(onChatGroupExpandedChanged),
                         &state);

        GtkWidget* group_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
        gtk_widget_add_css_class(group_box, "chat-group-list");
        for (const std::size_t index : indexes)
        {
            gtk_box_append(GTK_BOX(group_box),
                           makeConversationButton(snapshot.conversations[index],
                                                  index,
                                                  state));
        }
        gtk_expander_set_child(GTK_EXPANDER(expander), group_box);
        gtk_box_append(GTK_BOX(state.chat_conversation_list), expander);
    }
}

static void refreshChat(GtkUConsoleAppState& state)
{
    ChatWorkspaceSnapshot snapshot = state.chat_model.snapshot(
        kConversationLimit,
        kMessageLimit,
        state.chat_sort_mode);

    if (state.chat_sort_combo != nullptr &&
        gtk_combo_box_get_active(GTK_COMBO_BOX(state.chat_sort_combo)) !=
            sortModeIndex(state.chat_sort_mode))
    {
        gtk_combo_box_set_active(GTK_COMBO_BOX(state.chat_sort_combo),
                                 sortModeIndex(state.chat_sort_mode));
    }

    std::ostringstream message_signature;
    message_signature << snapshot.active_title << '\n'
                      << snapshot.active_meta;
    for (const auto& item : snapshot.messages)
    {
        message_signature << '\n'
                          << (item.outgoing ? '>' : '<')
                          << (item.failed ? '!' : '.') << item.sender << '\t'
                          << item.meta << '\t' << item.text;
    }
    const std::string next_message_signature = message_signature.str();
    const bool message_list_changed =
        next_message_signature != state.chat_message_signature;
    state.chat_message_signature = next_message_signature;

    setLabel(state.chat_title, snapshot.active_title);
    setLabel(state.chat_meta, snapshot.active_meta);
    setLabel(state.chat_status,
             snapshot.action_status.empty() ? "Ready."
                                            : snapshot.action_status);
    gtk_widget_set_sensitive(state.chat_send_button,
                             snapshot.can_send ? TRUE : FALSE);
    gtk_widget_set_sensitive(state.chat_entry, snapshot.can_send ? TRUE : FALSE);
    gtk_widget_set_sensitive(state.chat_add_contact_button,
                             snapshot.can_contact_active_peer ? TRUE : FALSE);
    gtk_widget_set_sensitive(state.chat_request_nodeinfo_button,
                             snapshot.can_request_nodeinfo ? TRUE : FALSE);
    gtk_widget_set_sensitive(state.chat_send_position_button,
                             snapshot.can_send_position ? TRUE : FALSE);
    gtk_widget_set_sensitive(state.chat_send_poi_button,
                             snapshot.can_send_poi ? TRUE : FALSE);

    refreshConversationGroups(state, snapshot);

    if (state.chat_node_box != nullptr)
    {
        clearBox(state.chat_node_box);
        if (snapshot.nodes.empty())
        {
            GtkWidget* empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_add_css_class(empty, "empty-state");
            gtk_box_append(GTK_BOX(empty),
                           makeLabel("No node info", "row-title"));
            gtk_box_append(GTK_BOX(empty),
                           makeLabel("NodeInfo, Position and MQTT node details appear here after they are decoded.",
                                     "row-meta",
                                     true));
            gtk_box_append(GTK_BOX(state.chat_node_box), empty);
        }
        for (const auto& node : snapshot.nodes)
        {
            gtk_box_append(GTK_BOX(state.chat_node_box),
                           buildChatNodeInfoCard(state, node));
        }
    }

    clearListBox(state.chat_message_list);
    if (snapshot.messages.empty())
    {
        GtkWidget* empty = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_add_css_class(empty, "empty-state");
        gtk_box_append(GTK_BOX(empty), makeLabel("No messages", "row-title"));
        gtk_box_append(GTK_BOX(empty),
                       makeLabel("This conversation has no stored messages.",
                                 "row-meta"));
        gtk_list_box_append(GTK_LIST_BOX(state.chat_message_list), empty);
    }
    for (const auto& item : snapshot.messages)
    {
        GtkWidget* shell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_add_css_class(shell, "chat-message-shell");
        gtk_widget_set_hexpand(shell, TRUE);

        GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_hexpand(spacer, TRUE);

        GtkWidget* bubble = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_widget_add_css_class(bubble, "chat-bubble");
        gtk_widget_add_css_class(bubble,
                                 item.failed
                                     ? "chat-bubble-failed"
                                     : (item.outgoing ? "chat-bubble-out"
                                                      : "chat-bubble-in"));
        gtk_widget_set_halign(bubble,
                              item.outgoing ? GTK_ALIGN_END : GTK_ALIGN_START);
        gtk_widget_set_hexpand(bubble, FALSE);

        GtkWidget* sender = makeLabel(item.sender.c_str(), "chat-sender");
        GtkWidget* text = makeLabel(item.text.c_str(), "chat-text", true);
        gtk_label_set_max_width_chars(GTK_LABEL(text), 56);
        GtkWidget* meta = makeLabel(item.meta.c_str(), "chat-message-meta");
        gtk_label_set_max_width_chars(GTK_LABEL(meta), 56);
        gtk_box_append(GTK_BOX(bubble), sender);
        gtk_box_append(GTK_BOX(bubble), text);
        gtk_box_append(GTK_BOX(bubble), meta);

        if (item.outgoing)
        {
            gtk_box_append(GTK_BOX(shell), spacer);
            gtk_box_append(GTK_BOX(shell), bubble);
        }
        else
        {
            gtk_box_append(GTK_BOX(shell), bubble);
            gtk_box_append(GTK_BOX(shell), spacer);
        }
        gtk_list_box_append(GTK_LIST_BOX(state.chat_message_list), shell);
    }
    if (message_list_changed && state.chat_message_scroll != nullptr)
    {
        g_idle_add(scrollChatTranscriptToBottom, state.chat_message_scroll);
    }
}

void refreshChatLogic(GtkUConsoleAppState& state,
                      const GtkUConsoleRefreshSnapshot&)
{
    refreshChat(state);
}

GtkUConsolePageLifecycle makeChatPageLifecycle()
{
    return {.name = "chat",
            .title = "Chat",
            .onLaunch = launchChatLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshChatLogic,
            .onDestroy = nullptr};
}

} // namespace trailmate::uconsole::gtk
