#include "platform/gtk/gtk_uconsole_pages.h"

namespace trailmate::uconsole::gtk
{

std::vector<GtkUConsolePageLifecycle> buildGtkUConsolePageRegistry()
{
    return {makeOverviewPageLifecycle(),
            makeChatPageLifecycle(),
            makeMapPageLifecycle(),
            makeHardwarePageLifecycle(),
            makeDataPageLifecycle(),
            makeLogsPageLifecycle(),
            makeSettingsPageLifecycle()};
}

} // namespace trailmate::uconsole::gtk
