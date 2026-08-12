#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <algorithm>
#include <string>

namespace trailmate::uconsole::gtk
{

namespace
{

void onDataOpenMapClicked(GtkButton*, gpointer data)
{
    showPage(*static_cast<GtkUConsoleAppState*>(data), "map");
}

void onDataRetryMapDownloadsClicked(GtkButton*, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    state.map_failed_tiles.clear();
    state.map_fetch_status = "state: retry requested";
    refreshUi(state);
}

GtkWidget* buildDataStatusRow(const char* category,
                              const std::string& value,
                              const std::string& detail,
                              bool needs_attention = false)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(row, "data-status-row");
    gtk_widget_set_hexpand(row, TRUE);

    GtkWidget* category_label = makeLabel(category, "data-status-category");
    gtk_widget_set_size_request(category_label, 108, -1);
    gtk_box_append(GTK_BOX(row), category_label);

    GtkWidget* value_label = makeLabel(value.c_str(), "data-status-value");
    gtk_widget_set_size_request(value_label, 94, -1);
    if (needs_attention)
    {
        gtk_widget_add_css_class(value_label, "data-status-attention");
    }
    gtk_box_append(GTK_BOX(row), value_label);

    GtkWidget* detail_label =
        makeLabel(detail.c_str(), "data-status-detail", true);
    gtk_widget_set_hexpand(detail_label, TRUE);
    gtk_box_append(GTK_BOX(row), detail_label);
    return row;
}

} // namespace

static void refreshDataPage(GtkUConsoleAppState& state,
                            const UConsoleDashboardSnapshot& dashboard,
                            const MapWorkspaceSnapshot& map_snapshot)
{
    clearBox(state.data_page_box);

    GtkWidget* inventory = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(inventory, "data-status-table");
    gtk_box_append(GTK_BOX(inventory),
                   buildDataStatusRow(
                       "Messages",
                       std::to_string(dashboard.conversation_count),
                       std::to_string(dashboard.unread_count) + " unread",
                       dashboard.unread_count > 0));
    gtk_box_append(GTK_BOX(inventory),
                   buildDataStatusRow(
                       "Contacts",
                       std::to_string(dashboard.contact_count),
                       std::to_string(dashboard.nearby_count) + " nearby / " +
                           std::to_string(dashboard.ignored_count) + " ignored"));
    gtk_box_append(
        GTK_BOX(inventory),
        buildDataStatusRow("Map cache",
                           std::to_string(map_snapshot.cache_stats.cached_tiles),
                           formatBytes(map_snapshot.cache_stats.total_bytes),
                           map_snapshot.cache_stats.failed_tiles > 0));
    gtk_box_append(GTK_BOX(inventory),
                   buildDataStatusRow(
                       "Storage",
                       "SQLite",
                       map_snapshot.cache_stats.database.filename().string()));
    gtk_box_append(GTK_BOX(state.data_page_box), inventory);

    const std::size_t visible_cached = static_cast<std::size_t>(std::count_if(
        map_snapshot.tiles.begin(),
        map_snapshot.tiles.end(),
        [](const MapTileItem& tile)
        { return tile.available; }));
    const std::size_t visible_total = map_snapshot.tiles.size();
    const std::size_t visible_missing = visible_total - visible_cached;

    GtkWidget* downloads = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(downloads, "data-operation-section");
    GtkWidget* download_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* download_title =
        makeLabel("Automatic offline map downloads", "pane-heading");
    gtk_widget_set_hexpand(download_title, TRUE);
    gtk_box_append(GTK_BOX(download_header), download_title);

    GtkWidget* retry = gtk_button_new_with_label("Retry failed");
    gtk_widget_add_css_class(retry, "nav-button");
    gtk_widget_set_sensitive(retry,
                             state.map_failed_tiles.empty() ? FALSE : TRUE);
    g_signal_connect(retry,
                     "clicked",
                     G_CALLBACK(onDataRetryMapDownloadsClicked),
                     &state);
    gtk_box_append(GTK_BOX(download_header), retry);

    GtkWidget* open_map = gtk_button_new_with_label("Open map");
    gtk_widget_add_css_class(open_map, "nav-button");
    g_signal_connect(open_map,
                     "clicked",
                     G_CALLBACK(onDataOpenMapClicked),
                     &state);
    gtk_box_append(GTK_BOX(download_header), open_map);
    gtk_box_append(GTK_BOX(downloads), download_header);

    GtkWidget* download_status = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(download_status, "data-status-table");
    gtk_box_append(GTK_BOX(download_status),
                   buildDataStatusRow(
                       "Visible tiles",
                       std::to_string(visible_cached) + " / " +
                           std::to_string(visible_total),
                       std::to_string(visible_missing) + " waiting",
                       visible_missing > 0));
    gtk_box_append(
        GTK_BOX(download_status),
        buildDataStatusRow("Downloads",
                           std::to_string(state.map_fetch_jobs.size()),
                           "Up to " +
                               std::to_string(kMaxConcurrentMapFetches) +
                               " concurrent"));
    gtk_box_append(
        GTK_BOX(download_status),
        buildDataStatusRow("Retry queue",
                           std::to_string(state.map_failed_tiles.size()),
                           state.map_failed_tiles.empty()
                               ? "Healthy"
                               : "Exponential backoff",
                           !state.map_failed_tiles.empty()));
    gtk_box_append(GTK_BOX(download_status),
                   buildDataStatusRow(
                       "Base layer",
                       map_snapshot.source_label,
                       "Zoom " + std::to_string(map_snapshot.zoom)));
    gtk_box_append(GTK_BOX(downloads), download_status);

    const std::string download_state =
        state.map_fetch_status.empty()
            ? "Idle; missing tiles are fetched automatically around the current map viewport."
            : state.map_fetch_status;
    gtk_box_append(GTK_BOX(downloads),
                   buildDetailRow("Background worker",
                                  download_state,
                                  !state.map_failed_tiles.empty()));
    gtk_box_append(GTK_BOX(downloads),
                   buildDetailRow(
                       "Cache policy",
                       "Fetch visible base tiles on demand, keep them on disk, and reuse them offline."));
    gtk_box_append(GTK_BOX(state.data_page_box), downloads);

    GtkWidget* detail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class(detail, "data-operation-section");
    gtk_box_append(GTK_BOX(detail),
                   makeLabel("Local data roots", "pane-heading"));
    gtk_box_append(GTK_BOX(detail),
                   buildDetailRow("SQLite database",
                                  map_snapshot.cache_stats.database.string()));
    gtk_box_append(GTK_BOX(detail),
                   buildDetailRow("Map cache root",
                                  map_snapshot.cache_stats.root.string()));
    gtk_box_append(GTK_BOX(detail),
                   buildDetailRow("Map cache health",
                                  std::to_string(
                                      map_snapshot.cache_stats.cached_tiles) +
                                      " cached / " +
                                      std::to_string(
                                          map_snapshot.cache_stats.failed_tiles) +
                                      " failed / " +
                                      formatBytes(
                                          map_snapshot.cache_stats.total_bytes),
                                  map_snapshot.cache_stats.failed_tiles > 0));
    gtk_box_append(GTK_BOX(state.data_page_box), detail);
}

void refreshDataLogic(GtkUConsoleAppState& state,
                      const GtkUConsoleRefreshSnapshot& snapshot)
{
    refreshDataPage(state, snapshot.dashboard, snapshot.map);
}

GtkUConsolePageLifecycle makeDataPageLifecycle()
{
    return {.name = "data",
            .title = "Data",
            .onLaunch = launchDataLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshDataLogic,
            .onDestroy = nullptr};
}

} // namespace trailmate::uconsole::gtk
