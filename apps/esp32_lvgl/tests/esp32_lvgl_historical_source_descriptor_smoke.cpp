#include "esp32_lvgl_historical_source_descriptor.h"

#include <cassert>
#include <cstring>

int main()
{
    const auto& descriptor =
        trailmate::apps::esp32_lvgl::esp32LvglHistoricalSourceDescriptor();

    assert(std::strcmp(descriptor.historical_root_name,
                       "removed root esp_idf") == 0);
    assert(std::strcmp(descriptor.historical_role,
                       "pre-refactor ESP-IDF/LVGL implementation root") == 0);
    assert(std::strcmp(descriptor.replacement_owner,
                       "apps/esp32_lvgl + builds/esp_idf") == 0);
    return 0;
}
