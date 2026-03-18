#pragma once

#include <cstddef>

#include "lvgl.h"
#include "ui/assets/fonts/fonts.h"
#include "ui/page/page_profile.h"

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_18) || !LV_FONT_MONTSERRAT_18
#define lv_font_montserrat_18 lv_font_montserrat_14
#endif

namespace ui::fonts
{

inline bool utf8_has_non_ascii(const char* text)
{
    if (!text)
    {
        return false;
    }

    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p != 0; ++p)
    {
        if (*p >= 0x80)
        {
            return true;
        }
    }
    return false;
}

inline const lv_font_t* ui_chrome_font()
{
    return ::ui::page_profile::current().large_touch_hitbox
               ? &lv_font_montserrat_18
               : &lv_font_montserrat_16;
}

inline const lv_font_t* chat_content_font(const char* text)
{
#if defined(GAT562_NO_CJK) && GAT562_NO_CJK
    (void)text;
    return ui_chrome_font();
#else
    return utf8_has_non_ascii(text) ? &lv_font_noto_cjk_16_2bpp : ui_chrome_font();
#endif
}

inline void apply_font(lv_obj_t* label, const lv_font_t* font)
{
    if (label && font)
    {
        lv_obj_set_style_text_font(label, font, 0);
    }
}

inline void apply_ui_chrome_font(lv_obj_t* label)
{
    apply_font(label, ui_chrome_font());
}

inline void apply_chat_content_font(lv_obj_t* label, const char* text)
{
    apply_font(label, chat_content_font(text));
}

} // namespace ui::fonts
