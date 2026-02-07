#pragma once

#include "lvgl.h"

namespace ui::theme
{
inline lv_color_t page_bg() { return lv_color_hex(0xFFF3DF); }
inline lv_color_t surface() { return lv_color_hex(0xFFF7E9); }
inline lv_color_t surface_alt() { return lv_color_hex(0xFFF0D3); }
inline lv_color_t border() { return lv_color_hex(0xD9B06A); }
inline lv_color_t separator() { return lv_color_hex(0xE8D2AB); }
inline lv_color_t accent() { return lv_color_hex(0xEBA341); }
inline lv_color_t text() { return lv_color_hex(0x3A2A1A); }
inline lv_color_t text_muted() { return lv_color_hex(0x6A5646); }
inline lv_color_t white() { return lv_color_hex(0xFFFFFF); }
inline lv_color_t status_green() { return lv_color_hex(0x5BAF4A); }
inline lv_color_t status_blue() { return lv_color_hex(0x2F6FD6); }
inline lv_color_t battery_green() { return lv_color_hex(0x3DBB56); }
inline lv_color_t map_bg() { return lv_color_hex(0xF6E7C8); }
inline lv_color_t error() { return lv_color_hex(0xCC0000); }
} // namespace ui::theme
