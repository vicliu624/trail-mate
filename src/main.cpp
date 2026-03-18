#include <Arduino.h>

#if defined(GAT562_MESH_EVB_PRO)
#include "apps/gat562_mesh_evb_pro/arduino_entry.h"
#else
#include "apps/esp_pio/arduino_entry.h"
#endif

void setup()
{
#if defined(GAT562_MESH_EVB_PRO)
    apps::gat562_mesh_evb_pro::arduino_entry::setup();
#else
    apps::esp_pio::arduino_entry::setup();
#endif
}

void loop()
{
#if defined(GAT562_MESH_EVB_PRO)
    apps::gat562_mesh_evb_pro::arduino_entry::loop();
#else
    apps::esp_pio::arduino_entry::loop();
#endif
}
