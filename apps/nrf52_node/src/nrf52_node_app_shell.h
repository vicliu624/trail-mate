#pragma once

#include "nrf52_historical_source_descriptor.h"

#include "product_composition/target_profile.h"

namespace trailmate
{
namespace apps
{
namespace nrf52_node
{

struct Nrf52NodeAppShellConfig
{
    const char* target_id = "gat562_mesh_evb_pro";
    const char* target_family = "nrf52_node";
    const char* default_ux_pack_id = "tiny_node_status";
    const char* historical_generic_root_name =
        nrf52HistoricalSourceDescriptor().historical_generic_root_name;
    const char* historical_board_root_name =
        nrf52HistoricalSourceDescriptor().historical_board_root_name;
    const char* historical_role =
        nrf52HistoricalSourceDescriptor().historical_role;
    const char* replacement_owner =
        nrf52HistoricalSourceDescriptor().replacement_owner;
};

class Nrf52NodeAppShell
{
  public:
    const Nrf52NodeAppShellConfig& config() const;
    const char* targetId() const;
    const product_composition::TargetProfile* targetProfile() const;
    const char* activeUxPackId() const;
    bool validate() const;

  private:
    Nrf52NodeAppShellConfig config_{};
};

} // namespace nrf52_node
} // namespace apps
} // namespace trailmate
