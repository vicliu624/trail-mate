#pragma once

#include "product_composition/target_profile.h"

#include <cstdint>

namespace trailmate::apps::nrf52_node
{

struct Nrf52NodeRuntimeConfig
{
    const char* target_id = "gat562_mesh_evb_pro";
    const product_composition::TargetProfile* target_profile = nullptr;
    std::int8_t lora_tx_power_max_dbm = 0;
    bool ble_enabled_by_default = true;
};

const Nrf52NodeRuntimeConfig& runtimeConfig();

} // namespace trailmate::apps::nrf52_node
