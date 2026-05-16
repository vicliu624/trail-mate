#include "product_composition/target_build_binding.h"

#include <cstring>

namespace product_composition
{
namespace
{

constexpr TargetBuildBinding kEsp32LvglBuildBindings[] = {
    {
        "tab5",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "builds/esp_idf/targets/tab5/sdkconfig.defaults",
    },
    {
        "tdisplayp4_tft",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults",
    },
    {
        "tdisplayp4_amoled",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "builds/esp_idf/targets/tdisplayp4_amoled/sdkconfig.defaults",
    },
};

} // namespace

const TargetBuildBinding* findTargetBuildBinding(const char* target_id)
{
    if (target_id == nullptr)
    {
        return nullptr;
    }

    for (const auto& binding : kEsp32LvglBuildBindings)
    {
        if (std::strcmp(binding.target_id, target_id) == 0)
        {
            return &binding;
        }
    }
    return nullptr;
}

const TargetBuildBinding* esp32LvglTargetBuildBindings(std::size_t* count)
{
    if (count != nullptr)
    {
        *count = sizeof(kEsp32LvglBuildBindings) / sizeof(kEsp32LvglBuildBindings[0]);
    }
    return kEsp32LvglBuildBindings;
}

} // namespace product_composition
