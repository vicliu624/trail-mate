#pragma once

namespace trailmate
{
namespace apps
{
namespace nrf52_node
{

struct Nrf52PioLegacyImplementationDescriptor
{
    const char* implementation_root = "apps/esp_pio";
    const char* board_specific_root = "apps/gat562_mesh_evb_pro";
    const char* app_shell = "apps/nrf52_node";
    const char* build_wrapper = "builds/pio_nrf52";
};

} // namespace nrf52_node
} // namespace apps
} // namespace trailmate
