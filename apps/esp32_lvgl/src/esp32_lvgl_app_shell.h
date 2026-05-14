#pragma once

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
    const char* transitional_source = "apps/esp_idf";
    const char* legacy_adapter_target = "trailmate_esp_idf_legacy_adapter";
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
