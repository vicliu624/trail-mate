#pragma once

namespace trailmate
{
namespace apps
{
namespace nrf52_node
{

struct Nrf52NodeAppShellConfig
{
    const char* target_family = "nrf52_node";
    const char* default_ux_pack_id = "tiny_node_status";
    const char* transitional_source = "apps/esp_pio";
    const char* board_specific_transitional_source = "apps/gat562_mesh_evb_pro";
    const char* legacy_adapter_target = "trailmate_nrf52_pio_legacy_adapter";
};

class Nrf52NodeAppShell
{
  public:
    const Nrf52NodeAppShellConfig& config() const;
    bool validate() const;

  private:
    Nrf52NodeAppShellConfig config_{};
};

} // namespace nrf52_node
} // namespace apps
} // namespace trailmate
