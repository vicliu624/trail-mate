/**
 * @file fonts.h
 * @brief Project custom fonts
 */

#pragma once

#include "lvgl.h"

#if defined(GAT562_NO_CJK) && GAT562_NO_CJK
#define lv_font_noto_cjk_16_2bpp lv_font_montserrat_14
#else
LV_FONT_DECLARE(lv_font_noto_cjk_16_2bpp);
#endif
