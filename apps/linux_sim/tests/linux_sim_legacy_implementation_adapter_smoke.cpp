#include "linux_sim_legacy_implementation_adapter.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::linux_sim::LinuxSimLegacyImplementationAdapter adapter;
    assert(adapter.validate());

    const auto& descriptor = adapter.descriptor();
    assert(std::strcmp(descriptor.implementation_root, "apps/linux_sim") == 0);
    assert(std::strcmp(descriptor.app_shell, "apps/linux_sim_shell") == 0);
    assert(std::strcmp(descriptor.target_id, "linux_sim") == 0);
    return 0;
}
