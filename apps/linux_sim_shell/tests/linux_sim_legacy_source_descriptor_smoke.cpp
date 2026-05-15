#include "linux_sim_legacy_source_descriptor.h"

#include <cassert>
#include <cstring>

int main()
{
    const auto& descriptor =
        trailmate::apps::linux_sim_shell::linuxSimLegacySourceDescriptor();

    assert(std::strcmp(descriptor.root_path,
                       "legacy/app_implementations/linux_sim") == 0);
    assert(std::strcmp(descriptor.historical_name, "linux_sim") == 0);
    assert(std::strcmp(descriptor.replacement_owner,
                       "apps/linux_sim_shell") == 0);
    assert(descriptor.compatibility_required);
    return 0;
}
