#include "nrf52_historical_source_descriptor.h"

namespace trailmate::apps::nrf52_node
{

const Nrf52HistoricalSourceDescriptor& nrf52HistoricalSourceDescriptor()
{
    static const Nrf52HistoricalSourceDescriptor descriptor{};
    return descriptor;
}

} // namespace trailmate::apps::nrf52_node
