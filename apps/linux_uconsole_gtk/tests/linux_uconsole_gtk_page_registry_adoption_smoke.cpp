#include "linux_uconsole_gtk_app_shell.h"
#include "linux_uconsole_gtk_page_registry_adoption.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell shell;
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkPageRegistryAdoption
        page_registry;

    assert(page_registry.load(shell));
    assert(page_registry.ready());
    assert(page_registry.usingPrimaryScreenGraph());
    assert(page_registry.registrySource() ==
           trailmate::apps::linux_uconsole_gtk::
               LinuxUConsoleGtkPageRegistrySource::ScreenGraphAdoption);
    assert(!page_registry.fallbackUsed());
    assert(page_registry.menuCount() > 0);
    assert(page_registry.screenCount() > 0);
    assert(page_registry.menuDescriptors()[0].route.valid);
    assert(page_registry.screenDescriptors()[0].binding_id != nullptr);
    return 0;
}
