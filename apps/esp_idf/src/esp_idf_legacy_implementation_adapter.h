#pragma once

namespace trailmate
{
namespace apps
{
namespace esp_idf
{

struct EspIdfLegacyImplementationDescriptor
{
    const char* implementation_root = "apps/esp_idf";
    const char* app_shell = "apps/esp32_lvgl";
    const char* build_wrapper = "builds/esp_idf";
};

class EspIdfLegacyImplementationAdapter
{
  public:
    const EspIdfLegacyImplementationDescriptor& descriptor() const;
    bool validate() const;

  private:
    EspIdfLegacyImplementationDescriptor descriptor_{};
};

} // namespace esp_idf
} // namespace apps
} // namespace trailmate
