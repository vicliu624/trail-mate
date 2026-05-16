#pragma once

#include "apps/esp_idf/runtime_config.h"
#include "esp32_lvgl_startup_runtime.h"

namespace apps::esp_idf::startup_runtime
{

inline void run(const RuntimeConfig& config)
{
    trailmate::apps::esp32_lvgl::runEsp32LvglStartupRuntime(config);
}

} // namespace apps::esp_idf::startup_runtime
