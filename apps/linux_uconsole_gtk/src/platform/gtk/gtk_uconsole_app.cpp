#include "platform/gtk/gtk_uconsole_app.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <gtk/gtk.h>

#include "platform/gtk/gtk_uconsole_app_state.h"
#include "platform/gtk/gtk_uconsole_shell.h"
#include "platform/gtk/gtk_uconsole_style.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{
namespace
{

void onActivate(GtkApplication* app, gpointer data)
{
    auto& state = *static_cast<GtkUConsoleAppState*>(data);
    installCss();

    state.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(state.window), state.options.title.c_str());
    gtk_window_set_resizable(GTK_WINDOW(state.window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(state.window),
                                std::max(320, state.options.width),
                                std::max(240, state.options.height));
    g_signal_connect(state.window, "destroy", G_CALLBACK(onWindowDestroy),
                     &state);

    if (!state.services.initialize())
    {
        GtkWidget* error = makeLabel("Startup failed.", "empty-state");
        gtk_window_set_child(GTK_WINDOW(state.window), error);
    }
    else
    {
        gtk_window_set_child(GTK_WINDOW(state.window), buildRoot(state));
        state.refresh_source = g_timeout_add(500, onRefresh, &state);
    }

    if (state.options.fullscreen)
    {
        gtk_window_fullscreen(GTK_WINDOW(state.window));
    }
    gtk_window_present(GTK_WINDOW(state.window));
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
