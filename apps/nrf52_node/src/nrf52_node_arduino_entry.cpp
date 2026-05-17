#include "nrf52_node_arduino_entry.h"

#include "nrf52_node_loop_runtime.h"
#include "nrf52_node_startup_runtime.h"

namespace trailmate::apps::nrf52_node::arduino_entry
{

void setup()
{
    startup_runtime::run();
}

void loop()
{
    loop_runtime::tick();
}

} // namespace trailmate::apps::nrf52_node::arduino_entry