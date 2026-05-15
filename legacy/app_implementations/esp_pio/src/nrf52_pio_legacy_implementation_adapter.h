#pragma once

namespace trailmate
{
namespace apps
{
namespace nrf52_node
{

struct Nrf52PioLegacyImplementationDescriptor
{
    const char* implementation_root = "legacy/app_implementations/esp_pio";
    const char* board_specific_root = "legacy/app_implementations/gat562_mesh_evb_pro";
    const char* app_shell = "apps/nrf52_node";
    const char* build_wrapper = "builds/pio_nrf52";
};

} // namespace nrf52_node
} // namespace apps
} // namespace trailmate
