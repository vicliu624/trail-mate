#pragma once

#include "apps/esp_idf/runtime_config.h"

namespace apps::esp_idf::app_facade_runtime
{

bool initialize(const RuntimeConfig& config);
void tick();
bool isBound();

} // namespace apps::esp_idf::app_facade_runtime
