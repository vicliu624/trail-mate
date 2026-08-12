#if defined(ARDUINO_T_DECK_PRO)

#include "ui/tdeck_pro/text_font.h"

extern "C"
{
    lv_font_t tdeck_pro_ui_font_16 = tdeck_pro_unifont_16;
}

namespace ui::tdeck_pro
{

const lv_font_t* text_font()
{
    return &tdeck_pro_ui_font_16;
}

void set_text_font_fallback(const lv_font_t* fallback)
{
    // Start from the immutable English bitmap every time. This prevents an
    // old font-pack chain from being retained after the locale changes.
    tdeck_pro_ui_font_16 = tdeck_pro_unifont_16;
    tdeck_pro_ui_font_16.fallback =
        fallback != &tdeck_pro_ui_font_16 ? fallback : nullptr;
}

} // namespace ui::tdeck_pro

#endif // defined(ARDUINO_T_DECK_PRO)
