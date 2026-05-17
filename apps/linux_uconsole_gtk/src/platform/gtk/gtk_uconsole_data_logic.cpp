#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <string>

namespace trailmate::uconsole::gtk
{

static void refreshDataPage(GtkUConsoleAppState& state,
                            const UConsoleDashboardSnapshot& dashboard,
                            const MapWorkspaceSnapshot& map_snapshot)
{
    clearBox(state.data_page_box);

    GtkWidget* strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(strip, "metric-strip");
    gtk_widget_set_hexpand(strip, TRUE);
    gtk_box_append(GTK_BOX(strip),
                   makeMetricCard("Messages",
                                  std::to_string(dashboard.conversation_count),
                                  std::to_string(dashboard.unread_count) +
                                      " unread",
                                  dashboard.unread_count > 0));
    gtk_box_append(GTK_BOX(strip),
                   makeMetricCard("Contacts",
                                  std::to_string(dashboard.contact_count),
                                  std::to_string(dashboard.nearby_count) +
                                      " nearby / " +
                                      std::to_string(dashboard.ignored_count) +
                                      " ignored"));
    gtk_box_append(GTK_BOX(strip),
                   makeMetricCard("Map cache",
                                  std::to_string(
                                      map_snapshot.cache_stats.cached_tiles),
                                  formatBytes(
                                      map_snapshot.cache_stats.total_bytes),
                                  map_snapshot.cache_stats.failed_tiles > 0));
    gtk_box_append(GTK_BOX(strip),
                   makeMetricCard("Storage", "SQLite",
                                  map_snapshot.cache_stats.database.filename()
                                      .string()));
    gtk_box_append(GTK_BOX(state.data_page_box), strip);

    GtkWidget* detail = makePanel();
    gtk_widget_add_css_class(detail, "inspector-pane");
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
