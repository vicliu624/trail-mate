#pragma once

#include "lvgl.h"

// English (U+0020-U+007E) is generated directly from GNU Unifont 17.0.05
// BDF cells at 16px/1bpp. Localized glyphs remain external font packs.
// Glyphs are rendered at their authored pixel size; no LVGL transform or
// anti-aliasing is used on the T-Deck Pro EPD path.
LV_FONT_DECLARE(tdeck_pro_unifont_16);

// The mutable proxy always uses the built-in English cells as its primary
// font. When a locale pack is activated, its 16px bitmap chain is assigned as
// the proxy fallback. Legacy shared pages can therefore use one native font
// name without bypassing the downloaded CJK glyphs.
#ifdef __cplusplus
extern "C"
{
#endif
    extern lv_font_t tdeck_pro_ui_font_16;
#ifdef __cplusplus
}
#endif

namespace ui::tdeck_pro
{

const lv_font_t* text_font();
void set_text_font_fallback(const lv_font_t* fallback);

} // namespace ui::tdeck_pro
