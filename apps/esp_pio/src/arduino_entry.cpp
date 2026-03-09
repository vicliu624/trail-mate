/**
 * @file arduino_entry.cpp
 * @brief Arduino setup/loop implementation for the PlatformIO app shell
 */

#include <Arduino.h>

#include "apps/esp_pio/arduino_entry.h"
#include "apps/esp_pio/loop_runtime.h"
#include "apps/esp_pio/startup_runtime.h"

#ifndef HAS_GPS
#define HAS_GPS 1
#endif
#ifndef HAS_SD
#define HAS_SD 1
#endif
#ifndef HAS_PSRAM
#define HAS_PSRAM 1
#endif

namespace apps::esp_pio::arduino_entry
{

void setup()
{
    apps::esp_pio::startup_runtime::run();
}

void loop()
{
    apps::esp_pio::loop_runtime::tick();
}

} // namespace apps::esp_pio::arduino_entry
