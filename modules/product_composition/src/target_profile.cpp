#include "product_composition/target_profile.h"

#include <cstring>

namespace product_composition
{
namespace
{

constexpr TargetProfile kTargetProfiles[] = {
    {
        "tab5",
        "tab5",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "tab5_touch",
        "tab5_touch_ui",
        "tab5_touch_manifest",
        "tab5_large_touch",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        TargetSupportStatus::ActiveWithFallback,
        true,
        true,
        false,
        false,
        true,
        true,
        true,
    },
    {
        "tdisplayp4_tft",
        "tdisplayp4",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "tdisplayp4_touch",
        "tdisplayp4_touch_ui",
        "tdisplayp4_touch_manifest",
        "tdisplayp4_touch",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        TargetSupportStatus::ActiveWithFallback,
        true,
        true,
        false,
        false,
        true,
        true,
        true,
    },
    {
        "tdisplayp4_amoled",
        "tdisplayp4",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "tdisplayp4_touch",
        "tdisplayp4_touch_ui",
        "tdisplayp4_touch_manifest",
        "tdisplayp4_touch",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        TargetSupportStatus::ActiveWithFallback,
        true,
        true,
        false,
        false,
        true,
        true,
        true,
    },
    {
        "tlora_pager",
        "tlora_pager",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "pager_compact",
        "pager_compact_ui",
        "pager_compact_manifest",
        "pager_compact",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        TargetSupportStatus::PendingHardwareValidation,
        true,
        false,
        true,
        false,
        true,
        true,
        true,
    },
    {
        "tdeck",
        "tdeck",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "deck_full",
        "deck_wide_ui",
        "deck_full_manifest",
        "deck_wide",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        TargetSupportStatus::PendingHardwareValidation,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
    },
    {
        "twatch",
        "twatch",
        "builds/esp_idf",
        "apps/esp32_lvgl",
        "watch_compact",
        "watch_compact_ui",
        "watch_compact_manifest",
        "watch_compact",
        TargetPlatform::EspIdf,
        TargetRenderer::Lvgl,
        TargetSupportStatus::PendingHardwareValidation,
        true,
        true,
        false,
        false,
        true,
        false,
        true,
    },
    {
        "uconsole",
        "uconsole",
        "builds/linux_cmake",
        "apps/linux_uconsole_gtk",
        "uconsole_desktop",
        "uconsole_desktop_ui",
        "uconsole_desktop_manifest",
        "uconsole_desktop",
        TargetPlatform::Linux,
        TargetRenderer::Gtk,
        TargetSupportStatus::Active,
        true,
        false,
        true,
        false,
        true,
        true,
        false,
    },
    {
        "cardputerzero",
        "cardputerzero",
        "builds/linux_cmake",
        "apps/linux_sim_shell",
        "cardputer_compact",
        "cardputer_compact_ui",
        "cardputer_compact_manifest",
        "cardputer_compact",
        TargetPlatform::Linux,
        TargetRenderer::Ascii,
        TargetSupportStatus::ActiveWithFallback,
        true,
        false,
        true,
        false,
        false,
        false,
        false,
    },
    {
        "gat562_mesh_evb_pro",
        "gat562_mesh_evb_pro",
        "builds/pio_nrf52",
        "apps/nrf52_node",
        "node_headless",
        "node_headless_ui",
        "node_headless_manifest",
        "headless_node",
        TargetPlatform::PlatformIo,
        TargetRenderer::Headless,
        TargetSupportStatus::Headless,
        true,
        false,
        false,
        false,
        true,
        true,
        false,
    },
};

} // namespace

const TargetProfile* findTargetProfile(const char* target_id)
{
    if (target_id == nullptr)
    {
        return nullptr;
    }

    for (const auto& profile : kTargetProfiles)
    {
        if (std::strcmp(profile.target_id, target_id) == 0)
        {
            return &profile;
        }
    }
    return nullptr;
}

const TargetProfile* allTargetProfiles(std::size_t* count)
{
    if (count != nullptr)
    {
        *count = sizeof(kTargetProfiles) / sizeof(kTargetProfiles[0]);
    }
    return kTargetProfiles;
}

const TargetProfile* esp32LvglTargetProfiles(std::size_t* count)
{
    static constexpr TargetProfile kEsp32LvglTargets[] = {
        kTargetProfiles[0],
        kTargetProfiles[1],
        kTargetProfiles[2],
        kTargetProfiles[3],
        kTargetProfiles[4],
        kTargetProfiles[5],
    };

    if (count != nullptr)
    {
        *count = sizeof(kEsp32LvglTargets) / sizeof(kEsp32LvglTargets[0]);
    }
    return kEsp32LvglTargets;
}

} // namespace product_composition
