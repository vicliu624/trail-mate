#include "linux_sim_app_shell.h"
#include "linux_sim_runtime_entry_adoption_probe.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_sim_shell::LinuxSimAppShell shell;
    trailmate::apps::linux_sim_shell::LinuxSimRuntimeEntryAdoptionProbe probe;

    assert(probe.load(shell));
    assert(probe.ready());
    assert(!probe.fallbackUsed());
    assert(probe.menuCount() > 0);
    assert(probe.screenCount() > 0);
    assert(probe.adoption().presenter().menuLines()[0].route.valid);
    return 0;
}
