#include "platform/gtk/gtk_uconsole_pages.h"

namespace trailmate::uconsole::gtk
{

std::vector<GtkUConsolePageLifecycle> buildGtkUConsolePageRegistry()
{
    return {makeChatPageLifecycle(),
            makeContactsPageLifecycle(),
            makeMapPageLifecycle(),
            makeGpsPageLifecycle(),
            makeTeamPageLifecycle(),
            makeTrackerPageLifecycle(),
            makeRadioToolsPageLifecycle(),
            makeExtensionsPageLifecycle(),
            makeHardwarePageLifecycle(),
            makeDataPageLifecycle(),
            makeLogsPageLifecycle(),
            makeSettingsPageLifecycle()};
}

} // namespace trailmate::uconsole::gtk
