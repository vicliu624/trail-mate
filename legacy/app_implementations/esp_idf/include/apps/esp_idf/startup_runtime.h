#pragma once

#include "apps/esp_idf/runtime_config.h"

namespace apps::esp_idf::startup_runtime
{

void run(const RuntimeConfig& config);

} // namespace apps::esp_idf::startup_runtime