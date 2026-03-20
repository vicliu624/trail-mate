#pragma once

#include "lvgl.h"

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif

namespace ui::menu::dashboard
{

struct DashboardCardChrome
{
    lv_obj_t* card = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* status_chip = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* footer = nullptr;
};

lv_color_t color_amber();
lv_color_t color_amber_dark();
lv_color_t color_panel_bg();
lv_color_t color_line();
lv_color_t color_text();
lv_color_t color_text_dim();
lv_color_t color_warn();
lv_color_t color_ok();
lv_color_t color_info();
lv_color_t color_soft_amber();
lv_color_t color_soft_blue();
lv_color_t color_soft_green();
lv_color_t color_soft_warn();

DashboardCardChrome create_card_chrome(lv_obj_t* parent,
                                       const char* title,
                                       lv_coord_t card_w,
                                       lv_coord_t card_h);
void set_status_chip(DashboardCardChrome& chrome, const char* text, lv_color_t bg, lv_color_t fg);
void style_stat_tile(lv_obj_t* tile, lv_color_t bg);
void style_body_label(lv_obj_t* label, const lv_font_t* font, lv_color_t color);
void style_footer_label(lv_obj_t* label);
void style_ring_object(lv_obj_t* obj, lv_coord_t diameter, lv_coord_t border, lv_color_t color);
void format_distance(double meters, char* out, size_t out_len);
float normalize_degrees(float value);
float bearing_between(double lat1, double lon1, double lat2, double lon2);
double haversine_m(double lat1, double lon1, double lat2, double lon2);
const char* compass_rose(float bearing);

} // namespace ui::menu::dashboard
