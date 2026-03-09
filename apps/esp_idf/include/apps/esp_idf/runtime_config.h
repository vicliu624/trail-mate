#pragma once

#include <cstdint>

namespace apps::esp_idf
{

struct RuntimeConfig
{
    const char* log_tag = "trail-mate-idf";
    const char* target_name = "esp-idf";
    const char* loop_task_name = "idf_app_loop";
    uint32_t loop_delay_ms = 10;
    uint32_t loop_stack_size = 4096;
    uint32_t loop_priority = 5;
};

namespace runtime_config
{

const RuntimeConfig& get();

} // namespace runtime_config

} // namespace apps::esp_idf
