#pragma once

#include <cstdint>

namespace product_composition
{
struct TargetProfile;
}

namespace trailmate::apps::esp32_lvgl
{

struct Esp32LvglRuntimeConfig
{
    const char* target_id = "esp_idf";
    const char* log_tag = "trail-mate-idf";
    const char* target_name = "esp-idf";
    const char* loop_task_name = "idf_app_loop";
    uint32_t loop_delay_ms = 10;
    uint32_t loop_stack_size = 4096;
    uint32_t loop_priority = 5;
};

const Esp32LvglRuntimeConfig& esp32LvglRuntimeConfig();
const product_composition::TargetProfile* esp32LvglRuntimeTargetProfile();
bool hasEsp32LvglRuntimeTargetProfile();

} // namespace trailmate::apps::esp32_lvgl
