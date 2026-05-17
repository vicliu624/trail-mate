#include "nrf52_node_runtime_config.h"

namespace trailmate::apps::nrf52_node
{
namespace
{

#if defined(TRAIL_MATE_LORA_TX_POWER_MAX_DBM)
constexpr std::int8_t kTargetLoraTxPowerMaxDbm =
    TRAIL_MATE_LORA_TX_POWER_MAX_DBM;
#else
constexpr std::int8_t kTargetLoraTxPowerMaxDbm = 0;
#endif

const Nrf52NodeRuntimeConfig kRuntimeConfig{
    "gat562_mesh_evb_pro",
    product_composition::findTargetProfile("gat562_mesh_evb_pro"),
    kTargetLoraTxPowerMaxDbm,
    true,
};

} // namespace

const Nrf52NodeRuntimeConfig& runtimeConfig()
{
    return kRuntimeConfig;
}

} // namespace trailmate::apps::nrf52_node
