#include "linux_uconsole_gtk_app_shell.h"
#include "linux_uconsole_gtk_page_registry_adoption.h"
#include "linux_uconsole_gtk_page_registry_renderer.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell shell;
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkPageRegistryAdoption
        adoption;
    assert(adoption.load(shell));
    assert(adoption.usingPrimaryScreenGraph());

    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkPageRegistryRenderer
        renderer;
    assert(renderer.render(adoption));
    assert(renderer.ready());
    assert(renderer.usingPrimaryScreenGraph());
    assert(renderer.usedPrimaryScreenGraph());
    assert(!renderer.fallbackUsed());
    assert(!renderer.usedFallback());
    assert(renderer.pageCount() > 0);
    assert(renderer.pages() != nullptr);
    assert(renderer.pages()[0].binding_id != nullptr);

    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShellConfig
        invalid_config;
    invalid_config.ux_pack_id = "missing_phase11_pack";
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell invalid_shell(
        invalid_config);
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkPageRegistryAdoption
        fallback_adoption;
    assert(!fallback_adoption.load(invalid_shell));
    assert(fallback_adoption.fallbackUsed());
    assert(!renderer.render(fallback_adoption));
    assert(renderer.fallbackUsed());
    assert(renderer.usedFallback());
    return 0;
}
