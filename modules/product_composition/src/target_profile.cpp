#include "product_composition/target_profile.h"

#include <cstring>

namespace product_composition
{
namespace
{

constexpr TargetProfile kEsp32LvglTargets[] = {
    {
        "tab5",
        "tab5",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        "compatibility",
    },
    {
        "tdisplayp4_tft",
        "t_display_p4",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        "compatibility",
    },
    {
        "tdisplayp4_amoled",
        "t_display_p4",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        "compatibility",
    },
};

} // namespace

const TargetProfile* findTargetProfile(const char* target_id)
{
    if (target_id == nullptr)
    {
        return nullptr;
    }

    for (const auto& profile : kEsp32LvglTargets)
    {
        if (std::strcmp(profile.target_id, target_id) == 0)
        {
            return &profile;
        }
    }
    return nullptr;
}

const TargetProfile* esp32LvglTargetProfiles(std::size_t* count)
{
    if (count != nullptr)
    {
        *count = sizeof(kEsp32LvglTargets) / sizeof(kEsp32LvglTargets[0]);
    }
    return kEsp32LvglTargets;
}

} // namespace product_composition
