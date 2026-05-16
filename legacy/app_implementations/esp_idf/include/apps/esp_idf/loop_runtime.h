#pragma once

#include "apps/esp_idf/runtime_config.h"
#include "esp32_lvgl_loop_runtime.h"

namespace apps::esp_idf::loop_runtime
{

inline void start(const RuntimeConfig& config)
{
    trailmate::apps::esp32_lvgl::startEsp32LvglLoopRuntime(config);
}

} // namespace apps::esp_idf::loop_runtime
