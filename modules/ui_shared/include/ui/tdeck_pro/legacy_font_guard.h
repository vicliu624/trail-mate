#pragma once

/*
 * T-Deck Pro only: legacy application pages still name LVGL's Montserrat
 * fonts directly.  The e-paper UI has one authored text grid (GNU Unifont,
 * 16px/1bpp), so make those historical requests resolve to that grid instead
 * of allowing a scaled, antialiased font to reach the panel.
 *
 * This header is force-included for ui_shared compilation units.  It is a
 * deliberate compatibility boundary: new Pro code should use text_font(),
 * while old shared pages remain visually safe until their layouts are
 * individually simplified.
 */

#if defined(ARDUINO_T_DECK_PRO)

#include "lvgl.h"

extern lv_font_t tdeck_pro_ui_font_16;

#define lv_font_montserrat_8 tdeck_pro_ui_font_16
#define lv_font_montserrat_10 tdeck_pro_ui_font_16
#define lv_font_montserrat_12 tdeck_pro_ui_font_16
#define lv_font_montserrat_14 tdeck_pro_ui_font_16
#define lv_font_montserrat_16 tdeck_pro_ui_font_16
#define lv_font_montserrat_18 tdeck_pro_ui_font_16
#define lv_font_montserrat_20 tdeck_pro_ui_font_16
#define lv_font_montserrat_22 tdeck_pro_ui_font_16
#define lv_font_montserrat_24 tdeck_pro_ui_font_16
#define lv_font_montserrat_26 tdeck_pro_ui_font_16
#define lv_font_montserrat_28 tdeck_pro_ui_font_16
#define lv_font_montserrat_30 tdeck_pro_ui_font_16
#define lv_font_montserrat_32 tdeck_pro_ui_font_16
#define lv_font_montserrat_34 tdeck_pro_ui_font_16
#define lv_font_montserrat_36 tdeck_pro_ui_font_16
#define lv_font_montserrat_38 tdeck_pro_ui_font_16
#define lv_font_montserrat_40 tdeck_pro_ui_font_16
#define lv_font_montserrat_42 tdeck_pro_ui_font_16
#define lv_font_montserrat_44 tdeck_pro_ui_font_16
#define lv_font_montserrat_46 tdeck_pro_ui_font_16
#define lv_font_montserrat_48 tdeck_pro_ui_font_16

#endif // defined(ARDUINO_T_DECK_PRO)
