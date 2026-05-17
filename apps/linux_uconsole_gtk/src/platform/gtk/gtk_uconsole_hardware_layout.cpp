#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* launchHardwareLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "Hardware",
        "uConsole/AIO2 endpoints and current driver binding state.",
        &state.hardware_page_box);
}

} // namespace trailmate::uconsole::gtk
