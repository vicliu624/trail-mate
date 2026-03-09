#include "platform/esp/idf_common/startup_support.h"

#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"

namespace platform::esp::idf_common::startup_support
{

void logStartupBanner(const char* tag)
{
    const esp_app_desc_t* desc = esp_app_get_description();
    ESP_LOGI(tag, "startup project=%s version=%s", desc->project_name, desc->version);
    ESP_LOGI(tag, "idf=%s compile_time=%s %s", esp_get_idf_version(), desc->date, desc->time);
}

} // namespace platform::esp::idf_common::startup_support
