#include "linux_sim_legacy_source_descriptor.h"

namespace trailmate::apps::linux_sim_shell
{

const LinuxSimLegacySourceDescriptor& linuxSimLegacySourceDescriptor()
{
    static const LinuxSimLegacySourceDescriptor descriptor{};
    return descriptor;
}

} // namespace trailmate::apps::linux_sim_shell
