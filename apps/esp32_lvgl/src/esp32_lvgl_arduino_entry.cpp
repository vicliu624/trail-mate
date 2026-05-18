#include "esp32_lvgl_arduino_entry.h"

#include "esp32_lvgl_arduino_loop_runtime.h"
#include "esp32_lvgl_arduino_startup_runtime.h"

namespace trailmate::apps::esp32_lvgl::arduino_entry
{

void setup()
{
    arduino_startup_runtime::run();
}

void loop()
{
    arduino_loop_runtime::tick();
}

} // namespace trailmate::apps::esp32_lvgl::arduino_entry
