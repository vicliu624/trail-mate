#include "linux_sim_app_shell.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::linux_sim_shell::LinuxSimAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_id, "linux_sim") == 0);
    assert(std::strcmp(config.ux_pack_id, "simulator_full") == 0);
    assert(std::strcmp(config.transitional_source, "apps/linux_sim") == 0);
    return 0;
}
