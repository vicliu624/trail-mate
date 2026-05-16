#pragma once

#include "esp32_lvgl_runtime_config.h"

namespace trailmate::apps::esp32_lvgl
{

bool canStartEsp32LvglLoopRuntime(const Esp32LvglRuntimeConfig& config);
void startEsp32LvglLoopRuntime(const Esp32LvglRuntimeConfig& config);

} // namespace trailmate::apps::esp32_lvgl
