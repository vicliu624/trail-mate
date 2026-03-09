#include <Arduino.h>

#include "apps/esp_pio/arduino_entry.h"

void setup()
{
    apps::esp_pio::arduino_entry::setup();
}

void loop()
{
    apps::esp_pio::arduino_entry::loop();
}
