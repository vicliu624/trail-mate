#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* launchLogsLayout(GtkUConsoleAppState& state)
{
    GtkWidget* root = makeWorkbench(GTK_ORIENTATION_VERTICAL, 6);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(toolbar, "log-toolbar");
    state.logs_source_lora = gtk_button_new_with_label("LoRa");
    gtk_widget_add_css_class(state.logs_source_lora, "nav-button");
    g_signal_connect(state.logs_source_lora, "clicked",
                     G_CALLBACK(onLogsSourceLoraClicked), &state);
    gtk_box_append(GTK_BOX(toolbar), state.logs_source_lora);
    state.logs_source_mqtt = gtk_button_new_with_label("MQTT");
    gtk_widget_add_css_class(state.logs_source_mqtt, "nav-button");
    g_signal_connect(state.logs_source_mqtt, "clicked",
                     G_CALLBACK(onLogsSourceMqttClicked), &state);
    gtk_box_append(GTK_BOX(toolbar), state.logs_source_mqtt);
    state.logs_source_gps = gtk_button_new_with_label("GPS");
    gtk_widget_add_css_class(state.logs_source_gps, "nav-button");
    g_signal_connect(state.logs_source_gps, "clicked",
                     G_CALLBACK(onLogsSourceGpsClicked), &state);
    gtk_box_append(GTK_BOX(toolbar), state.logs_source_gps);
    gtk_box_append(GTK_BOX(root), toolbar);

    state.logs_page_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(state.logs_page_box, TRUE);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),
                                  state.logs_page_box);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(root), scroll);
    return root;
}

} // namespace trailmate::uconsole::gtk
