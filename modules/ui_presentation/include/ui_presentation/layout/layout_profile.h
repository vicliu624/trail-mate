#pragma once

#include <cstddef>

namespace ui::presentation
{

struct LayoutProfile
{
    const char* layout_profile_id;
    int screen_width;
    int screen_height;
    bool use_large_touch_targets;
    bool use_compact_rows;
    bool use_icon_only_nav;
    bool use_bottom_nav;
    bool use_side_nav;
    bool use_watch_cards;
    bool use_keyboard_shortcuts;
    int max_visible_menu_items;
    int primary_font_size;
    int secondary_font_size;
};

const LayoutProfile* findLayoutProfile(const char* layout_profile_id);
const LayoutProfile* allLayoutProfiles(std::size_t* count);

} // namespace ui::presentation
