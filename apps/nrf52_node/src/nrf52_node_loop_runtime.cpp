#include "nrf52_node_loop_runtime.h"

#include <Arduino.h>

#include "nrf52_node_app_runtime_access.h"

namespace trailmate::apps::nrf52_node::loop_runtime
{

void tick()
{
    app_runtime_access::tick();
    delay(2);
}

} // namespace trailmate::apps::nrf52_node::loop_runtime