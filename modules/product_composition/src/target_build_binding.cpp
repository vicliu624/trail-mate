#include "product_composition/target_build_binding.h"

#include <cstring>

namespace product_composition
{
namespace
{

constexpr TargetBuildBinding kTargetBuildBindings[] = {
    {
        "tab5",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "builds/esp_idf/targets/tab5/sdkconfig.defaults",
        "migrated",
    },
    {
        "tdisplayp4_tft",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults",
        "migrated",
    },
    {
        "tdisplayp4_amoled",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "builds/esp_idf/targets/tdisplayp4_amoled/sdkconfig.defaults",
        "migrated",
    },
    {
        "tlora_pager",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        nullptr,
        "pending_target_defaults",
    },
    {
        "tdeck",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        nullptr,
        "pending_target_defaults",
    },
    {
        "twatch",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        nullptr,
        "pending_target_defaults",
    },
    {
        "uconsole",
        "builds/linux_cmake",
        "apps/linux_uconsole_gtk",
        nullptr,
        "linux_cmake",
    },
    {
        "cardputerzero",
        "builds/linux_cmake",
        "apps/linux_sim_shell",
        nullptr,
        "linux_fallback_shell",
    },
    {
        "gat562_mesh_evb_pro",
        "builds/pio_nrf52",
        "apps/nrf52_node",
        nullptr,
        "platformio",
    },
};

} // namespace

const TargetBuildBinding* findTargetBuildBinding(const char* target_id)
{
    if (target_id == nullptr)
    {
        return nullptr;
    }

    for (const auto& binding : kTargetBuildBindings)
    {
        if (std::strcmp(binding.target_id, target_id) == 0)
        {
            return &binding;
        }
    }
    return nullptr;
}

const TargetBuildBinding* allTargetBuildBindings(std::size_t* count)
{
    if (count != nullptr)
    {
        *count = sizeof(kTargetBuildBindings) / sizeof(kTargetBuildBindings[0]);
    }
    return kTargetBuildBindings;
}

const TargetBuildBinding* esp32LvglTargetBuildBindings(std::size_t* count)
{
    static constexpr TargetBuildBinding kEsp32LvglBuildBindings[] = {
        kTargetBuildBindings[0],
        kTargetBuildBindings[1],
        kTargetBuildBindings[2],
        kTargetBuildBindings[3],
        kTargetBuildBindings[4],
        kTargetBuildBindings[5],
    };

    if (count != nullptr)
    {
        *count = sizeof(kEsp32LvglBuildBindings) / sizeof(kEsp32LvglBuildBindings[0]);
    }
    return kEsp32LvglBuildBindings;
}

} // namespace product_composition
