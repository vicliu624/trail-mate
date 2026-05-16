#pragma once

#include "esp32_lvgl_historical_source_descriptor.h"

namespace trailmate
{
namespace apps
{
namespace esp32_lvgl
{

struct Esp32LvglAppShellConfig
{
    const char* target_family = "esp32_lvgl";
    const char* default_ux_pack_id = "compatibility";
    const char* build_entrypoint = "builds/esp_idf";
    const char* component_sources = "builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake";
    const char* historical_root_name =
        esp32LvglHistoricalSourceDescriptor().historical_root_name;
    const char* historical_role =
        esp32LvglHistoricalSourceDescriptor().historical_role;
    const char* replacement_owner =
        esp32LvglHistoricalSourceDescriptor().replacement_owner;
};

class Esp32LvglAppShell
{
  public:
    const Esp32LvglAppShellConfig& config() const;
    const char* activeUxPackId() const;
    bool validate() const;

  private:
    Esp32LvglAppShellConfig config_{};
};

} // namespace esp32_lvgl
} // namespace apps
} // namespace trailmate
