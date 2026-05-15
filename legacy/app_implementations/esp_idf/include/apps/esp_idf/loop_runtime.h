#pragma once

#include "apps/esp_idf/runtime_config.h"

namespace apps::esp_idf::loop_runtime
{

void start(const RuntimeConfig& config);

} // namespace apps::esp_idf::loop_runtime