#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::runtime
{

enum class MemoryProfileKind : uint8_t
{
    Constrained = 0,
    Standard,
    Extended,
};

struct MemoryProfile
{
    const char* name = "constrained";
    MemoryProfileKind kind = MemoryProfileKind::Constrained;

    std::size_t max_locale_font_ram_bytes = 0;
    std::size_t max_content_supplement_ram_bytes = 0;
    std::size_t max_content_supplement_packs = 0;

    std::size_t max_map_decode_tiles = 0;
    bool retain_map_decode_cache_on_page_exit = false;
};

const MemoryProfile& current_memory_profile();

inline bool allows_content_supplements()
{
    const MemoryProfile& profile = current_memory_profile();
    return profile.max_content_supplement_packs > 0 && profile.max_content_supplement_ram_bytes > 0;
}

} // namespace ui::runtime
