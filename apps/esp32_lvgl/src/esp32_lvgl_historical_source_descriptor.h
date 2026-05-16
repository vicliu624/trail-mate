#pragma once

namespace trailmate::apps::esp32_lvgl
{

struct Esp32LvglHistoricalSourceDescriptor
{
    const char* historical_root_name = "removed root esp_idf";
    const char* historical_role = "pre-refactor ESP-IDF/LVGL implementation root";
    const char* replacement_owner = "apps/esp32_lvgl + builds/esp_idf";
};

const Esp32LvglHistoricalSourceDescriptor&
esp32LvglHistoricalSourceDescriptor();

} // namespace trailmate::apps::esp32_lvgl
