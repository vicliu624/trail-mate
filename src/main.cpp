#include <Arduino.h>

#if defined(GAT562_MESH_EVB_PRO)
#include "nrf52_node_arduino_entry.h"
#else
#include "esp32_lvgl_arduino_entry.h"
#endif

void setup()
{
#if defined(GAT562_MESH_EVB_PRO)
    trailmate::apps::nrf52_node::arduino_entry::setup();
#else
    trailmate::apps::esp32_lvgl::arduino_entry::setup();
#endif
}

void loop()
{
#if defined(GAT562_MESH_EVB_PRO)
    trailmate::apps::nrf52_node::arduino_entry::loop();
#else
    trailmate::apps::esp32_lvgl::arduino_entry::loop();
#endif
}
