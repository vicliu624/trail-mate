#include "nrf52_historical_source_descriptor.h"

#include <cassert>
#include <cstring>

int main()
{
    const auto& descriptor =
        trailmate::apps::nrf52_node::nrf52HistoricalSourceDescriptor();

    assert(std::strcmp(descriptor.historical_generic_root_name,
                       "removed root esp_pio") == 0);
    assert(std::strcmp(descriptor.historical_board_root_name,
                       "removed root gat562_mesh_evb_pro") == 0);
    assert(std::strcmp(descriptor.historical_role,
                       "pre-refactor PlatformIO/nRF52 implementation roots") == 0);
    assert(std::strcmp(descriptor.replacement_owner,
                       "apps/nrf52_node + builds/pio_nrf52 + boards/gat562_mesh_evb_pro") == 0);
    return 0;
}
