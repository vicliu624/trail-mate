#include "ui/runtime/memory_profile.h"
#include "ui/ux_build_config.h"

namespace ui::runtime
{
namespace
{

MemoryProfile make_constrained_profile()
{
    MemoryProfile profile{};
    profile.name = "constrained";
    profile.kind = MemoryProfileKind::Constrained;
    profile.max_locale_font_ram_bytes = 128U * 1024U;
    profile.max_content_supplement_ram_bytes = 0;
    profile.max_content_supplement_packs = 0;
    profile.max_map_decode_tiles = 2;
    profile.retain_map_decode_cache_on_page_exit = false;
    return profile;
}

MemoryProfile make_standard_profile()
{
    MemoryProfile profile{};
    profile.name = "standard";
    profile.kind = MemoryProfileKind::Standard;
    profile.max_locale_font_ram_bytes = 768U * 1024U;
    profile.max_content_supplement_ram_bytes = 640U * 1024U;
    // More small subsets may share the same unchanged RAM budget.
    profile.max_content_supplement_packs = 8;
    // 320x240 and pager touch map views need visible 256px tiles plus pan cushion.
    profile.max_map_decode_tiles = 12;
    profile.retain_map_decode_cache_on_page_exit = false;
    return profile;
}

MemoryProfile make_extended_profile()
{
    MemoryProfile profile{};
    profile.name = "extended";
    profile.kind = MemoryProfileKind::Extended;
    profile.max_locale_font_ram_bytes = 2U * 1024U * 1024U;
    profile.max_content_supplement_ram_bytes = 2U * 1024U * 1024U;
    profile.max_content_supplement_packs = 8;
    profile.max_map_decode_tiles = 12;
    profile.retain_map_decode_cache_on_page_exit = true;
    return profile;
}

} // namespace

const MemoryProfile& current_memory_profile()
{
    static const MemoryProfile profile = []()
    {
#if defined(TRAIL_MATE_PLATFORM_LINUX) && defined(TRAIL_MATE_LINUX_FREETYPE) && \
    TRAIL_MATE_LINUX_FREETYPE && !defined(ESP_PLATFORM) && !defined(ARDUINO_ARCH_ESP32)
    // Keep the established extended resource-policy name. Full CJK faces
    // have larger metadata; their glyph images use a bounded lazy cache.
#if defined(TRAIL_MATE_UI_P4_TOUCH)
        auto linux_profile = make_extended_profile();
#else
        auto linux_profile = make_constrained_profile();
#endif
        linux_profile.name = "extended";
        linux_profile.kind = MemoryProfileKind::Extended;
        linux_profile.max_locale_font_ram_bytes = 64U * 1024U * 1024U;
        linux_profile.max_content_supplement_ram_bytes = 64U * 1024U * 1024U;
        linux_profile.max_content_supplement_packs = 8;
        return linux_profile;
#elif defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_UI_P4_TOUCH)
        return make_extended_profile();
#elif defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO) ||                   \
    defined(ARDUINO_LILYGO_LORA_SX1262) || defined(ARDUINO_LILYGO_LORA_LR1121) || \
    defined(ARDUINO_LILYGO_T_WATCH_S3) ||                                         \
    defined(BOARD_HAS_PSRAM) || defined(HAS_PSRAM)
        return make_standard_profile();
#else
        return make_constrained_profile();
#endif
    }();

    return profile;
}

} // namespace ui::runtime
