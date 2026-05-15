#pragma once

#include <gtk/gtk.h>

#include "platform/gtk/gtk_uconsole_app_state.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* buildRoot(GtkUConsoleAppState& state);
void showPage(GtkUConsoleAppState& state, const char* page);
void refreshUi(GtkUConsoleAppState& state);
gboolean onRefresh(gpointer data);
void onWindowDestroy(GtkWidget*, gpointer data);
void shutdownGtkUConsoleApp(GtkUConsoleAppState& state);

} // namespace trailmate::uconsole::gtk
