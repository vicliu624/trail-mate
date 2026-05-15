#include "linux_sim_app_shell.h"
#include "linux_sim_runtime_entry.h"
#include "linux_sim_runtime_renderer.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_sim_shell::LinuxSimAppShell shell;
    trailmate::apps::linux_sim_shell::LinuxSimRuntimeEntry entry;
    assert(entry.start(shell));
    assert(entry.usingPrimaryScreenGraph());

    trailmate::apps::linux_sim_shell::LinuxSimRuntimeRenderer renderer;
    assert(renderer.render(entry));
    assert(renderer.ready());
    assert(renderer.usingPrimaryScreenGraph());
    assert(renderer.usedPrimaryScreenGraph());
    assert(!renderer.fallbackUsed());
    assert(!renderer.usedFallback());
    assert(renderer.lineCount() > 0);
    assert(renderer.lines() != nullptr);
    assert(renderer.line(0) != nullptr);

    trailmate::apps::linux_sim_shell::LinuxSimAppShellConfig invalid_config;
    invalid_config.ux_pack_id = "missing_phase11_pack";
    trailmate::apps::linux_sim_shell::LinuxSimAppShell invalid_shell(
        invalid_config);
    trailmate::apps::linux_sim_shell::LinuxSimRuntimeEntry fallback_entry;
    assert(!fallback_entry.start(invalid_shell));
    assert(fallback_entry.fallbackUsed());
    assert(!renderer.render(fallback_entry));
    assert(renderer.fallbackUsed());
    assert(renderer.usedFallback());
    return 0;
}
