#include "linux_sim_historical_source_descriptor.h"

#include <cassert>
#include <cstring>

int main()
{
    const auto& descriptor =
        trailmate::apps::linux_sim_shell::linuxSimHistoricalSourceDescriptor();

    assert(std::strcmp(descriptor.historical_root_name,
                       "removed root linux_sim") == 0);
    assert(std::strcmp(descriptor.historical_role,
                       "pre-refactor Linux simulator implementation root") == 0);
    assert(std::strcmp(descriptor.replacement_owner,
                       "apps/linux_sim_shell + modules/ui_ascii_runtime") == 0);
    return 0;
}
