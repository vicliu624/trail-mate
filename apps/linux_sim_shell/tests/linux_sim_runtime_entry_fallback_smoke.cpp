#include "linux_sim_app_shell.h"
#include "linux_sim_runtime_entry.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_sim_shell::LinuxSimAppShellConfig invalid_config;
    invalid_config.ux_pack_id = "missing_phase10_pack";
    trailmate::apps::linux_sim_shell::LinuxSimAppShell shell(invalid_config);
    trailmate::apps::linux_sim_shell::LinuxSimRuntimeEntry runtime_entry;

    assert(!runtime_entry.start(shell));
    assert(!runtime_entry.ready());
    assert(!runtime_entry.usingPrimaryScreenGraph());
    assert(runtime_entry.runtimeSource() ==
           trailmate::apps::linux_sim_shell::LinuxSimRuntimeSource::
               HardcodedFallback);
    assert(runtime_entry.fallbackUsed());
    assert(runtime_entry.menuCount() == 0);
    assert(runtime_entry.screenCount() == 0);
    return 0;
}
