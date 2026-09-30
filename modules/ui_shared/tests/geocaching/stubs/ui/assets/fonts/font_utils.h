#pragma once
#include "lvgl.h"
namespace ui::fonts
{
inline const lv_font_t* ui_chrome_font() { return &lv_font_montserrat_14; }
inline const lv_font_t* localized_font(const lv_font_t* base) { return base; }
inline const lv_font_t* content_font(const char*, const lv_font_t* base) { return base; }
inline void apply_content_font(lv_obj_t* object, const char*, const lv_font_t* font)
{
    lv_obj_set_style_text_font(object, font, 0);
}
inline void apply_font(lv_obj_t* object, const lv_font_t* font)
{
    lv_obj_set_style_text_font(object, font, 0);
}
} // namespace ui::fonts
