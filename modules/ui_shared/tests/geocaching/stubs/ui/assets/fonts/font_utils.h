#pragma once
#include "lvgl.h"
namespace ui::fonts
{
inline void apply_content_font(lv_obj_t* object, const char*, const lv_font_t* font)
{
    lv_obj_set_style_text_font(object, font, 0);
}
inline void apply_font(lv_obj_t* object, const lv_font_t* font)
{
    lv_obj_set_style_text_font(object, font, 0);
}
} // namespace ui::fonts
