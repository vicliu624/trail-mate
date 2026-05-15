#include "linux_uconsole_gtk_app_shell.h"
#include "linux_uconsole_gtk_runtime_entry_adoption_probe.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell shell;
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkRuntimeEntryAdoptionProbe
        probe;

    assert(probe.load(shell));
    assert(probe.ready());
    assert(!probe.fallbackUsed());
    assert(probe.menuCount() > 0);
    assert(probe.screenCount() > 0);
    assert(probe.adoption().presenter().menuDescriptors()[0].route.valid);
    return 0;
}
