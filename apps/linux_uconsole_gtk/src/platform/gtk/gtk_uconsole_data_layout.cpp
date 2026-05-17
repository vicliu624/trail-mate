#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* launchDataLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "Data",
        "Local SQLite state, message/contact counts, and map cache.",
        &state.data_page_box);
}

} // namespace trailmate::uconsole::gtk
