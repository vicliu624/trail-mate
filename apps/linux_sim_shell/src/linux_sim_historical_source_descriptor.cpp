#include "linux_sim_historical_source_descriptor.h"

namespace trailmate::apps::linux_sim_shell
{

const LinuxSimHistoricalSourceDescriptor& linuxSimHistoricalSourceDescriptor()
{
    static const LinuxSimHistoricalSourceDescriptor descriptor{};
    return descriptor;
}

} // namespace trailmate::apps::linux_sim_shell
