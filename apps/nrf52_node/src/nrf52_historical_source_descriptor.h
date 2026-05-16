#pragma once

namespace trailmate::apps::nrf52_node
{

struct Nrf52HistoricalSourceDescriptor
{
    const char* historical_generic_root_name = "removed root esp_pio";
    const char* historical_board_root_name = "removed root gat562_mesh_evb_pro";
    const char* historical_role = "pre-refactor PlatformIO/nRF52 implementation roots";
    const char* replacement_owner =
        "apps/nrf52_node + builds/pio_nrf52 + boards/gat562_mesh_evb_pro";
};

const Nrf52HistoricalSourceDescriptor& nrf52HistoricalSourceDescriptor();

} // namespace trailmate::apps::nrf52_node
