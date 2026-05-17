#include "nrf52_node_arduino_entry.h"

// The nRF52 app shell library keeps the wrapper baseline small. The real
// GAT562 environment consumes these final-owner runtime sources explicitly.
#include "../../../apps/nrf52_node/src/nrf52_node_app_facade_runtime.cpp"
#include "../../../apps/nrf52_node/src/nrf52_node_app_runtime_access.cpp"
#include "../../../apps/nrf52_node/src/nrf52_node_arduino_entry.cpp"
#include "../../../apps/nrf52_node/src/nrf52_node_loop_runtime.cpp"
#include "../../../apps/nrf52_node/src/nrf52_node_startup_runtime.cpp"
#include "../../../apps/nrf52_node/src/nrf52_node_ui_runtime.cpp"

extern "C" void setup()
{
    trailmate::apps::nrf52_node::arduino_entry::setup();
}

extern "C" void loop()
{
    trailmate::apps::nrf52_node::arduino_entry::loop();
}
