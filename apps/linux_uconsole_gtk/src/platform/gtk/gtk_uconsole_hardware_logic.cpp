#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <cstddef>

namespace trailmate::uconsole::gtk
{

void appendHardwareTableRow(GtkWidget* table,
                            const HardwareStatusItem& item,
                            int row)
{
    GtkWidget* name = makeLabel(item.name.c_str(), "hardware-table-name");
    GtkWidget* state = makeLabel(item.state.c_str(), "hardware-table-state");
    if (item.attention)
    {
        gtk_widget_add_css_class(state, "hardware-state-alert");
    }
    GtkWidget* detail = makeLabel(item.detail.c_str(), "row-meta", true);
    gtk_widget_set_hexpand(detail, TRUE);
    gtk_grid_attach(GTK_GRID(table), name, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(table), state, 1, row, 1, 1);
    gtk_grid_attach(GTK_GRID(table), detail, 2, row, 1, 1);
}
static void refreshHardwarePage(GtkUConsoleAppState& state,
                                const UConsoleDashboardSnapshot& snapshot)
{
    clearBox(state.hardware_page_box);

    GtkWidget* table = gtk_grid_new();
    gtk_widget_add_css_class(table, "hardware-table");
    gtk_grid_set_row_spacing(GTK_GRID(table), 0);
    gtk_grid_set_column_spacing(GTK_GRID(table), 12);
    gtk_widget_set_hexpand(table, TRUE);
    gtk_grid_attach(GTK_GRID(table),
                    makeLabel("Hardware", "hardware-table-header"),
                    0,
                    0,
                    1,
                    1);
    gtk_grid_attach(GTK_GRID(table),
                    makeLabel("State", "hardware-table-header"),
                    1,
                    0,
                    1,
                    1);
    gtk_grid_attach(GTK_GRID(table),
                    makeLabel("Driver / endpoint", "hardware-table-header"),
                    2,
                    0,
                    1,
                    1);
    for (std::size_t index = 0; index < snapshot.hardware.size(); ++index)
    {
        appendHardwareTableRow(table,
                               snapshot.hardware[index],
                               static_cast<int>(index) + 1);
    }
    gtk_box_append(GTK_BOX(state.hardware_page_box), table);

    GtkWidget* detail = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(detail, "hardware-capabilities");
    gtk_box_append(GTK_BOX(detail),
                   makeLabel("Capability and driver state", "pane-heading"));
    for (const auto& line : snapshot.capability_lines)
    {
        gtk_box_append(GTK_BOX(detail), makeLabel(line.c_str(), "row-meta",
                                                  true));
    }
    gtk_box_append(GTK_BOX(state.hardware_page_box), detail);
}

void refreshHardwareLogic(GtkUConsoleAppState& state,
                          const GtkUConsoleRefreshSnapshot& snapshot)
{
    refreshHardwarePage(state, snapshot.dashboard);
}

GtkUConsolePageLifecycle makeHardwarePageLifecycle()
{
    return {.name = "hardware",
            .title = "Hardware",
            .onLaunch = launchHardwareLayout,
            .onShow = nullptr,
            .onHide = nullptr,
            .onRefresh = refreshHardwareLogic,
            .onDestroy = nullptr};
}

} // namespace trailmate::uconsole::gtk
