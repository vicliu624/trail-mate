#include "platform/gtk/gtk_uconsole_app.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

#include <gtk/gtk.h>

#include "platform/gtk/gtk_uconsole_app_state.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_style.h"
#include "platform/gtk/gtk_uconsole_widgets.h"
#include "uconsole/uconsole_hardware_probe.h"

namespace trailmate::uconsole::gtk
{
namespace
{

void onMinimizeClicked(GtkButton*, gpointer data)
{
    gtk_window_minimize(GTK_WINDOW(data));
}

void onFullscreenClicked(GtkButton* button, gpointer data)
{
    GtkWindow* window = GTK_WINDOW(data);
    const gboolean fullscreen =
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(window), "uconsole-fullscreen"));
    if (fullscreen)
    {
        gtk_window_unfullscreen(window);
        g_object_set_data(G_OBJECT(window), "uconsole-fullscreen",
                          GINT_TO_POINTER(FALSE));
        gtk_button_set_label(button, "⛶");
        gtk_widget_set_tooltip_text(GTK_WIDGET(button), "Enter fullscreen");
    }
    else
    {
        gtk_window_fullscreen(window);
        g_object_set_data(G_OBJECT(window), "uconsole-fullscreen",
                          GINT_TO_POINTER(TRUE));
        gtk_button_set_label(button, "⛶");
        gtk_widget_set_tooltip_text(GTK_WIDGET(button), "Leave fullscreen");
    }
}

void onCloseClicked(GtkButton*, gpointer data)
{
    gtk_window_destroy(GTK_WINDOW(data));
}

void applyAio2GpsReceiverDefault(GtkUConsoleAppState& state)
{
    if (!isUsingUConsoleAio2DefaultGps())
    {
        return;
    }

    state.services.config().gps_init_baud =
        static_cast<std::uint32_t>(uConsoleAio2DefaultGpsBaud());
    state.services.applyPositionConfig();
}

gboolean onKeyPressed(GtkEventControllerKey*,
                      guint keyval,
                      guint,
                      GdkModifierType modifiers,
                      gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    return handleUConsoleShortcut(state, keyval, modifiers)
               ? GDK_EVENT_STOP
               : GDK_EVENT_PROPAGATE;
}

GtkWidget* makeWindowControl(const char* label, const char* tooltip)
{
    GtkWidget* button = gtk_button_new_with_label(label);
    gtk_widget_add_css_class(button, "titlebar-button");
    gtk_widget_set_tooltip_text(button, tooltip);
    gtk_widget_set_focusable(button, FALSE);
    return button;
}

GtkWidget* buildTitlebar(GtkWindow* window, const std::string& title)
{
    GtkWidget* header = gtk_header_bar_new();
    gtk_widget_add_css_class(header, "uconsole-titlebar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header), FALSE);

    GtkWidget* title_label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(title_label, "titlebar-title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title_label);

    GtkWidget* minimize = makeWindowControl("—", "Minimize window");
    g_signal_connect(minimize, "clicked", G_CALLBACK(onMinimizeClicked),
                     window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), minimize);

    const bool fullscreen_active =
        GPOINTER_TO_INT(
            g_object_get_data(G_OBJECT(window), "uconsole-fullscreen")) != 0;
    GtkWidget* fullscreen = makeWindowControl(
        "⛶", fullscreen_active ? "Leave fullscreen" : "Enter fullscreen");
    g_signal_connect(fullscreen, "clicked", G_CALLBACK(onFullscreenClicked),
                     window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), fullscreen);

    GtkWidget* close = makeWindowControl("×", "Close window");
    gtk_widget_add_css_class(close, "titlebar-close");
    g_signal_connect(close, "clicked", G_CALLBACK(onCloseClicked), window);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), close);

    return header;
}

GtkWidget* buildWindowContent(GtkUConsoleAppState& state, GtkWidget* content)
{
    GtkWidget* shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(shell, TRUE);
    gtk_widget_set_vexpand(shell, TRUE);

    GtkWidget* titlebar =
        buildTitlebar(GTK_WINDOW(state.window), state.options.title);
    gtk_box_append(GTK_BOX(shell), titlebar);

    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_vexpand(content, TRUE);
    gtk_box_append(GTK_BOX(shell), content);
    return shell;
}

void onActivate(GtkApplication* app, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    installCss();

    state.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state.window), state.options.title.c_str());
    gtk_window_set_resizable(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_decorated(GTK_WINDOW(state.window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(state.window),
                                std::max(320, state.options.width),
                                std::max(240, state.options.height));
    g_object_set_data(
        G_OBJECT(state.window), "uconsole-fullscreen",
        GINT_TO_POINTER(state.options.fullscreen ? TRUE : FALSE));
    g_signal_connect(state.window, "destroy", G_CALLBACK(onWindowDestroy),
                     &state);
    GtkEventController* key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_controller,
                                               GTK_PHASE_BUBBLE);
    g_signal_connect(key_controller, "key-pressed", G_CALLBACK(onKeyPressed),
                     &state);
    gtk_widget_add_controller(state.window, key_controller);

    if (!state.services.initialize())
    {
        GtkWidget* error = makeLabel("Startup failed.", "empty-state");
        gtk_window_set_child(GTK_WINDOW(state.window),
                             buildWindowContent(state, error));
    }
    else
    {
        applyAio2GpsReceiverDefault(state);
        gtk_window_set_child(GTK_WINDOW(state.window),
                             buildWindowContent(state, buildRoot(state)));
        state.refresh_source = g_timeout_add(500, onRefresh, &state);
    }

    gtk_window_present(GTK_WINDOW(state.window));
    if (state.options.fullscreen)
    {
        gtk_window_fullscreen(GTK_WINDOW(state.window));
    }
    else
    {
        gtk_window_maximize(GTK_WINDOW(state.window));
    }
}

} // namespace

int runGtkUConsoleApp(GtkUConsoleOptions options)
{
    auto state = std::make_unique<GtkUConsoleAppState>(std::move(options));
    GtkApplication* app =
        gtk_application_new("dev.trailmate.uconsole",
                            G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(onActivate), state.get());

    const int status = g_application_run(G_APPLICATION(app), 0, nullptr);
    shutdownGtkUConsoleApp(*state);
    g_object_unref(app);
    return status;
}

} // namespace trailmate::uconsole::gtk
