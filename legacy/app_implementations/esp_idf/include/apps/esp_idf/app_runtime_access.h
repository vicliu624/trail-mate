#pragma once

#include "apps/esp_idf/runtime_config.h"

namespace apps::esp_idf::app_runtime_access
{

struct Status
{
    bool initialized = false;
    bool board_handles_ready = false;
    bool lifecycle_ready = false;
    bool app_context_bound = false;
};

void initialize(const RuntimeConfig& config);
void tick();
const Status& status();

} // namespace apps::esp_idf::app_runtime_access
