#include "linux_uconsole_gtk_app_shell.h"
#include "linux_uconsole_gtk_page_registry_adoption.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShellConfig
        invalid_config;
    invalid_config.ux_pack_id = "missing_phase10_pack";
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell shell(
        invalid_config);
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkPageRegistryAdoption
        page_registry;

    assert(!page_registry.load(shell));
    assert(!page_registry.ready());
    assert(!page_registry.usingPrimaryScreenGraph());
    assert(page_registry.registrySource() ==
           trailmate::apps::linux_uconsole_gtk::
               LinuxUConsoleGtkPageRegistrySource::HardcodedFallback);
    assert(page_registry.fallbackUsed());
    assert(page_registry.menuCount() == 0);
    assert(page_registry.screenCount() == 0);
    return 0;
}
