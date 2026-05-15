#include "linux_sim_app_shell.h"
#include "linux_sim_runtime_entry.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_sim_shell::LinuxSimAppShell shell;
    trailmate::apps::linux_sim_shell::LinuxSimRuntimeEntry runtime_entry;

    assert(runtime_entry.start(shell));
    assert(runtime_entry.ready());
    assert(runtime_entry.usingPrimaryScreenGraph());
    assert(runtime_entry.runtimeSource() ==
           trailmate::apps::linux_sim_shell::LinuxSimRuntimeSource::
               ScreenGraphAdoption);
    assert(!runtime_entry.fallbackUsed());
    assert(runtime_entry.menuCount() > 0);
    assert(runtime_entry.screenCount() > 0);
    assert(runtime_entry.adoption().presenter().menuLines()[0].route.valid);
    return 0;
}
