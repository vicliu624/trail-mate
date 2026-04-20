#include "ui/runtime/memory_profile.h"

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
    profile.max_content_supplement_packs = 1;
    profile.max_map_decode_tiles = 4;
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
    profile.max_content_supplement_packs = 3;
    profile.max_map_decode_tiles = 12;
    profile.retain_map_decode_cache_on_page_exit = true;
    return profile;
}

} // namespace

const MemoryProfile& current_memory_profile()
{
    static const MemoryProfile profile = []()
    {
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        return make_extended_profile();
#elif defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
        return make_standard_profile();
#else
        return make_constrained_profile();
#endif
    }();

    return profile;
}

} // namespace ui::runtime
