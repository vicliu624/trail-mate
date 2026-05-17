#include "nrf52_node_runtime_config.h"

#include <cassert>

int main()
{
    const auto& config = trailmate::apps::nrf52_node::runtimeConfig();
    assert(config.target_id != nullptr);
    assert(config.target_profile != nullptr);
    return 0;
}
