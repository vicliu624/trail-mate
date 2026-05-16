#include "ui_presentation/layout/layout_profile.h"

#include <cstring>

namespace ui::presentation
{
namespace
{

constexpr LayoutProfile kLayoutProfiles[] = {
    {"tab5_large_touch", 0, 0, true, false, false, true, false, false, false, 8, 18, 14},
    {"tdisplayp4_touch", 0, 0, true, false, false, true, false, false, false, 7, 18, 14},
    {"pager_compact", 480, 222, false, true, true, false, false, false, true, 6, 14, 11},
    {"deck_wide", 320, 240, false, true, false, false, false, false, true, 8, 14, 11},
    {"watch_compact", 240, 240, false, true, true, false, false, true, false, 5, 13, 10},
    {"uconsole_desktop", 0, 0, false, false, false, false, true, false, true, 8, 15, 12},
    {"cardputer_compact", 320, 170, false, true, false, false, false, false, true, 6, 13, 10},
    {"headless_node", 0, 0, false, false, false, false, false, false, false, 0, 0, 0},
};

} // namespace

const LayoutProfile* findLayoutProfile(const char* layout_profile_id)
{
    if (layout_profile_id == nullptr)
    {
        return nullptr;
    }

    for (const auto& profile : kLayoutProfiles)
    {
        if (std::strcmp(profile.layout_profile_id, layout_profile_id) == 0)
        {
            return &profile;
        }
    }
    return nullptr;
}

const LayoutProfile* allLayoutProfiles(std::size_t* count)
{
    if (count != nullptr)
    {
        *count = sizeof(kLayoutProfiles) / sizeof(kLayoutProfiles[0]);
    }
    return kLayoutProfiles;
}

} // namespace ui::presentation
