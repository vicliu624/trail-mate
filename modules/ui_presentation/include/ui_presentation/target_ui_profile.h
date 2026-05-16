#pragma once

#include <cstddef>

namespace ui::presentation
{

enum class DisplayClass
{
    SmallPortrait,
    SmallLandscape,
    WideLandscape,
    SquareWatch,
    LargeTouch,
    Desktop,
    Headless,
};

enum class InputClass
{
    ButtonsOnly,
    TouchOnly,
    Keyboard,
    KeyboardTrackball,
    RotaryOrCrown,
    DesktopMouseKeyboard,
    Headless,
};

enum class LayoutClass
{
    PagerCompact,
    DeckWide,
    WatchCompact,
    TouchLarge,
    DesktopGtk,
    CardputerCompact,
    HeadlessNode,
};

struct TargetUiProfile
{
    const char* ui_profile_id;
    const char* target_family_id;
    int screen_width;
    int screen_height;
    DisplayClass display_class;
    InputClass input_class;
    LayoutClass layout_class;
    bool has_status_bar;
    bool has_bottom_nav;
    bool has_side_nav;
    bool has_soft_keyboard;
    bool has_pointer;
    bool has_touch;
    bool has_physical_keyboard;
    bool has_trackball;
    const char* page_manifest_id;
    const char* layout_profile_id;
};

const TargetUiProfile* findTargetUiProfile(const char* ui_profile_id);
const TargetUiProfile* allTargetUiProfiles(std::size_t* count);

} // namespace ui::presentation
