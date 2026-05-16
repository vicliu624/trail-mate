#pragma once

#include "esp32_lvgl_runtime_config.h"

namespace trailmate::apps::esp32_lvgl
{

bool canRunEsp32LvglStartupRuntime(const Esp32LvglRuntimeConfig& config);
void runEsp32LvglStartupRuntime(const Esp32LvglRuntimeConfig& config);

} // namespace trailmate::apps::esp32_lvgl
