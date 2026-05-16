#pragma once

#include "esp32_lvgl_runtime_config.h"

namespace apps::esp_idf
{

using RuntimeConfig = trailmate::apps::esp32_lvgl::Esp32LvglRuntimeConfig;

namespace runtime_config
{

inline const RuntimeConfig& get()
{
    return trailmate::apps::esp32_lvgl::esp32LvglRuntimeConfig();
}

} // namespace runtime_config

} // namespace apps::esp_idf
