#pragma once

#include <stdint.h>

namespace ui
{

enum class PresentationProfileKind : uint8_t
{
    CompactHandheld,
    Watch,
    TerminalPanel,
    DesktopWorkbench,
    Headless,
};

struct PresentationProfile
{
    PresentationProfileKind kind = PresentationProfileKind::CompactHandheld;
    uint16_t width = 0;
    uint16_t height = 0;
    bool color = true;
    bool rich_text = false;
    bool pointer_input = false;
    bool keyboard_input = false;
};

} // namespace ui
