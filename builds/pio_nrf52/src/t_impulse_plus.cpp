#include "nrf52_node_arduino_entry.h"

extern "C" void setup()
{
    trailmate::apps::nrf52_node::arduino_entry::setup();
}

extern "C" void loop()
{
    trailmate::apps::nrf52_node::arduino_entry::loop();
}
