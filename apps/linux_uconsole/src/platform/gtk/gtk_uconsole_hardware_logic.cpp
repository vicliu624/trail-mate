#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

#include <cstddef>

namespace trailmate::uconsole::gtk
{

GtkWidget* buildHardwareCard(const HardwareStatusItem& item)
{
    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
    gtk_widget_add_css_class(card, "hardware-card");
    if (item.attention)
    {
        gtk_widget_add_css_class(card, "hardware-card-alert");
    }
    gtk_widget_set_hexpand(card, TRUE);

    gtk_box_append(GTK_BOX(card), makeLabel(item.name.c_str(), "metric-label"));
    GtkWidget* state_label = makeLabel(item.state.c_str(), "hardware-state");
    if (item.attention)
    {
        gtk_widget_add_css_class(state_label, "hardware-state-alert");
    }
    gtk_box_append(GTK_BOX(card), state_label);
    gtk_box_append(GTK_BOX(card),
                   makeLabel(item.detail.c_str(), "row-meta", true));
    return card;
}
static void refreshHardwarePage(GtkUConsoleAppState& state,
                                const UConsoleDashboardSnapshot& snapshot)
{
    clearBox(state.hardware_page_box);

    GtkWidget* grid = gtk_grid_new();
    gtk_widget_add_css_class(grid, "detail-grid");
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_widget_set_hexpand(grid, TRUE);
    for (std::size_t index = 0; index < snapshot.hardware.size(); ++index)
    {
        const auto& item = snapshot.hardware[index];
        gtk_grid_attach(GTK_GRID(grid),
                        buildHardwareCard(item),
                        static_cast<int>(index % 3U),
                        static_cast<int>(index / 3U),
                        1,
                        1);
    }
    gtk_box_append(GTK_BOX(state.hardware_page_box), grid);

    GtkWidget* detail = makePanel();
    gtk_widget_add_css_class(detail, "inspector-pane");
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
