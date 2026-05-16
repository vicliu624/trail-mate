#include "ui_presentation/target_ui_profile.h"

#include <cstring>

namespace ui::presentation
{
namespace
{

constexpr TargetUiProfile kTargetUiProfiles[] = {
    {"tab5_touch_ui",
     "tab5",
     0,
     0,
     DisplayClass::LargeTouch,
     InputClass::TouchOnly,
     LayoutClass::TouchLarge,
     true,
     true,
     false,
     true,
     false,
     true,
     false,
     false,
     "tab5_touch_manifest",
     "tab5_large_touch"},
    {"tdisplayp4_touch_ui",
     "tdisplayp4",
     0,
     0,
     DisplayClass::LargeTouch,
     InputClass::TouchOnly,
     LayoutClass::TouchLarge,
     true,
     true,
     false,
     true,
     false,
     true,
     false,
     false,
     "tdisplayp4_touch_manifest",
     "tdisplayp4_touch"},
    {"pager_compact_ui",
     "tlora_pager",
     480,
     222,
     DisplayClass::SmallLandscape,
     InputClass::Keyboard,
     LayoutClass::PagerCompact,
     true,
     false,
     false,
     false,
     false,
     false,
     true,
     false,
     "pager_compact_manifest",
     "pager_compact"},
    {"deck_wide_ui",
     "tdeck",
     320,
     240,
     DisplayClass::WideLandscape,
     InputClass::KeyboardTrackball,
     LayoutClass::DeckWide,
     true,
     false,
     false,
     false,
     false,
     true,
     true,
     true,
     "deck_full_manifest",
     "deck_wide"},
    {"watch_compact_ui",
     "twatch",
     240,
     240,
     DisplayClass::SquareWatch,
     InputClass::TouchOnly,
     LayoutClass::WatchCompact,
     true,
     false,
     false,
     false,
     false,
     true,
     false,
     false,
     "watch_compact_manifest",
     "watch_compact"},
    {"uconsole_desktop_ui",
     "uconsole",
     0,
     0,
     DisplayClass::Desktop,
     InputClass::DesktopMouseKeyboard,
     LayoutClass::DesktopGtk,
     true,
     false,
     true,
     false,
     true,
     false,
     true,
     false,
     "uconsole_desktop_manifest",
     "uconsole_desktop"},
    {"cardputer_compact_ui",
     "cardputerzero",
     320,
     170,
     DisplayClass::WideLandscape,
     InputClass::Keyboard,
     LayoutClass::CardputerCompact,
     true,
     false,
     false,
     false,
     false,
     false,
     true,
     false,
     "cardputer_compact_manifest",
     "cardputer_compact"},
    {"node_headless_ui",
     "gat562_mesh_evb_pro",
     0,
     0,
     DisplayClass::Headless,
     InputClass::Headless,
     LayoutClass::HeadlessNode,
     false,
     false,
     false,
     false,
     false,
     false,
     false,
     false,
     "node_headless_manifest",
     "headless_node"},
};

} // namespace

const TargetUiProfile* findTargetUiProfile(const char* ui_profile_id)
{
    if (ui_profile_id == nullptr)
    {
        return nullptr;
    }

    for (const auto& profile : kTargetUiProfiles)
    {
        if (std::strcmp(profile.ui_profile_id, ui_profile_id) == 0)
        {
            return &profile;
        }
    }
    return nullptr;
}

const TargetUiProfile* allTargetUiProfiles(std::size_t* count)
{
    if (count != nullptr)
    {
        *count = sizeof(kTargetUiProfiles) / sizeof(kTargetUiProfiles[0]);
    }
    return kTargetUiProfiles;
}

} // namespace ui::presentation
