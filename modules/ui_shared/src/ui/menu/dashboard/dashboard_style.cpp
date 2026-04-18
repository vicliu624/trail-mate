#include "ui/menu/dashboard/dashboard_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui/assets/fonts/font_utils.h"

namespace ui::menu::dashboard
{
namespace
{

constexpr float kPi = 3.14159265358979323846f;

float deg_to_rad(float value)
{
    return value * (kPi / 180.0f);
}

float rad_to_deg(float value)
{
    return value * (180.0f / kPi);
}

} // namespace

lv_color_t color_amber() { return lv_color_hex(0xEBA341); }
lv_color_t color_amber_dark() { return lv_color_hex(0xC98118); }
lv_color_t color_panel_bg() { return lv_color_hex(0xFAF0D8); }
lv_color_t color_line() { return lv_color_hex(0xE7C98F); }
lv_color_t color_text() { return lv_color_hex(0x6B4A1E); }
lv_color_t color_text_dim() { return lv_color_hex(0x8A6A3A); }
lv_color_t color_warn() { return lv_color_hex(0xB94A2C); }
lv_color_t color_ok() { return lv_color_hex(0x3E7D3E); }
lv_color_t color_info() { return lv_color_hex(0x2D6FB6); }
lv_color_t color_soft_amber() { return lv_color_hex(0xF3D39C); }
lv_color_t color_soft_blue() { return lv_color_hex(0xDCE8F7); }
lv_color_t color_soft_green() { return lv_color_hex(0xDCEFD8); }
lv_color_t color_soft_warn() { return lv_color_hex(0xF5D9D1); }

DashboardCardChrome create_card_chrome(lv_obj_t* parent,
                                       const char* title,
                                       lv_coord_t card_w,
                                       lv_coord_t card_h)
{
    DashboardCardChrome chrome{};
    constexpr lv_coord_t kHeaderH = 30;
    constexpr lv_coord_t kFooterH = 28;

    chrome.card = lv_obj_create(parent);
    lv_obj_set_size(chrome.card, card_w, card_h);
    lv_obj_set_style_bg_color(chrome.card, color_panel_bg(), 0);
    lv_obj_set_style_bg_opa(chrome.card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chrome.card, color_line(), 0);
    lv_obj_set_style_border_width(chrome.card, 1, 0);
    lv_obj_set_style_radius(chrome.card, 18, 0);
    lv_obj_set_style_pad_all(chrome.card, 0, 0);
    lv_obj_set_style_shadow_width(chrome.card, 0, 0);
    lv_obj_clear_flag(chrome.card, LV_OBJ_FLAG_SCROLLABLE);

    chrome.header = lv_obj_create(chrome.card);
    lv_obj_set_size(chrome.header, LV_PCT(100), kHeaderH);
    lv_obj_align(chrome.header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(chrome.header, color_amber(), 0);
    lv_obj_set_style_bg_opa(chrome.header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chrome.header, 0, 0);
    lv_obj_set_style_radius(chrome.header, 18, 0);
    lv_obj_set_style_pad_left(chrome.header, 10, 0);
    lv_obj_set_style_pad_right(chrome.header, 10, 0);
    lv_obj_set_style_pad_top(chrome.header, 6, 0);
    lv_obj_set_style_pad_bottom(chrome.header, 6, 0);
    lv_obj_clear_flag(chrome.header, LV_OBJ_FLAG_SCROLLABLE);

    chrome.title = lv_label_create(chrome.header);
    lv_label_set_text(chrome.title, title);
    lv_obj_set_style_text_color(chrome.title, lv_color_hex(0x2A1A05), 0);
    lv_obj_set_style_text_font(chrome.title, &lv_font_montserrat_14, 0);
    ::ui::fonts::apply_localized_font(chrome.title, title, &lv_font_montserrat_14);
    lv_obj_align(chrome.title, LV_ALIGN_LEFT_MID, 0, 0);

    chrome.status_chip = lv_obj_create(chrome.header);
    lv_obj_set_size(chrome.status_chip, LV_SIZE_CONTENT, 18);
    lv_obj_align(chrome.status_chip, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(chrome.status_chip, 9, 0);
    lv_obj_set_style_bg_color(chrome.status_chip, color_panel_bg(), 0);
    lv_obj_set_style_bg_opa(chrome.status_chip, LV_OPA_80, 0);
    lv_obj_set_style_border_width(chrome.status_chip, 0, 0);
    lv_obj_set_style_pad_left(chrome.status_chip, 8, 0);
    lv_obj_set_style_pad_right(chrome.status_chip, 8, 0);
    lv_obj_set_style_pad_top(chrome.status_chip, 1, 0);
    lv_obj_set_style_pad_bottom(chrome.status_chip, 1, 0);
    lv_obj_clear_flag(chrome.status_chip, LV_OBJ_FLAG_SCROLLABLE);

    chrome.status_label = lv_label_create(chrome.status_chip);
    lv_label_set_text(chrome.status_label, "--");
    lv_obj_set_style_text_color(chrome.status_label, color_text(), 0);
    lv_obj_set_style_text_font(chrome.status_label, &lv_font_montserrat_12, 0);
    ::ui::fonts::apply_localized_font(chrome.status_label, "--", &lv_font_montserrat_12);
    lv_obj_center(chrome.status_label);

    chrome.body = lv_obj_create(chrome.card);
    lv_obj_set_size(chrome.body, LV_PCT(100), card_h - kHeaderH - kFooterH);
    lv_obj_align_to(chrome.body, chrome.header, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(chrome.body, color_panel_bg(), 0);
    lv_obj_set_style_bg_opa(chrome.body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chrome.body, 0, 0);
    lv_obj_set_style_pad_left(chrome.body, 10, 0);
    lv_obj_set_style_pad_right(chrome.body, 10, 0);
    lv_obj_set_style_pad_top(chrome.body, 8, 0);
    lv_obj_set_style_pad_bottom(chrome.body, 6, 0);
    lv_obj_clear_flag(chrome.body, LV_OBJ_FLAG_SCROLLABLE);

    chrome.footer = lv_obj_create(chrome.card);
    lv_obj_set_size(chrome.footer, LV_PCT(100), kFooterH);
    lv_obj_align(chrome.footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(chrome.footer, color_soft_amber(), 0);
    lv_obj_set_style_bg_opa(chrome.footer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chrome.footer, 0, 0);
    lv_obj_set_style_radius(chrome.footer, 18, 0);
    lv_obj_set_style_pad_left(chrome.footer, 10, 0);
    lv_obj_set_style_pad_right(chrome.footer, 10, 0);
    lv_obj_set_style_pad_top(chrome.footer, 5, 0);
    lv_obj_set_style_pad_bottom(chrome.footer, 5, 0);
    lv_obj_clear_flag(chrome.footer, LV_OBJ_FLAG_SCROLLABLE);

    return chrome;
}

void set_status_chip(DashboardCardChrome& chrome, const char* text, lv_color_t bg, lv_color_t fg)
{
    if (chrome.status_chip == nullptr || chrome.status_label == nullptr)
    {
        return;
    }
    if (lv_color_to_int(lv_obj_get_style_bg_color(chrome.status_chip, LV_PART_MAIN)) != lv_color_to_int(bg))
    {
        lv_obj_set_style_bg_color(chrome.status_chip, bg, 0);
    }
    if (lv_obj_get_style_bg_opa(chrome.status_chip, LV_PART_MAIN) != LV_OPA_COVER)
    {
        lv_obj_set_style_bg_opa(chrome.status_chip, LV_OPA_COVER, 0);
    }
    if (lv_color_to_int(lv_obj_get_style_text_color(chrome.status_label, LV_PART_MAIN)) != lv_color_to_int(fg))
    {
        lv_obj_set_style_text_color(chrome.status_label, fg, 0);
    }
    const char* current = lv_label_get_text(chrome.status_label);
    if (text != nullptr && (current == nullptr || std::strcmp(current, text) != 0))
    {
        lv_label_set_text(chrome.status_label, text);
        ::ui::fonts::apply_localized_font(chrome.status_label, text, &lv_font_montserrat_12);
    }
}

void style_stat_tile(lv_obj_t* tile, lv_color_t bg)
{
    lv_obj_set_style_bg_color(tile, bg, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_radius(tile, 14, 0);
    lv_obj_set_style_pad_left(tile, 10, 0);
    lv_obj_set_style_pad_right(tile, 10, 0);
    lv_obj_set_style_pad_top(tile, 6, 0);
    lv_obj_set_style_pad_bottom(tile, 6, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
}

void style_body_label(lv_obj_t* label, const lv_font_t* font, lv_color_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    ::ui::fonts::apply_localized_font(label, lv_label_get_text(label), font);
}

void style_footer_label(lv_obj_t* label)
{
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(label, color_text_dim(), 0);
    ::ui::fonts::apply_localized_font(label, lv_label_get_text(label), &lv_font_montserrat_12);
}

void style_ring_object(lv_obj_t* obj, lv_coord_t diameter, lv_coord_t border, lv_color_t color)
{
    lv_obj_set_size(obj, diameter, diameter);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, border, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void format_distance(double meters, char* out, size_t out_len)
{
    if (meters >= 1000.0)
    {
        std::snprintf(out, out_len, "%.1f km", meters / 1000.0);
    }
    else
    {
        std::snprintf(out, out_len, "%.0f m", meters);
    }
}

float normalize_degrees(float value)
{
    while (value < 0.0f)
    {
        value += 360.0f;
    }
    while (value >= 360.0f)
    {
        value -= 360.0f;
    }
    return value;
}

float bearing_between(double lat1, double lon1, double lat2, double lon2)
{
    const double lat1_rad = deg_to_rad(static_cast<float>(lat1));
    const double lat2_rad = deg_to_rad(static_cast<float>(lat2));
    const double dlon = deg_to_rad(static_cast<float>(lon2 - lon1));
    const double y = std::sin(dlon) * std::cos(lat2_rad);
    const double x = std::cos(lat1_rad) * std::sin(lat2_rad) -
                     std::sin(lat1_rad) * std::cos(lat2_rad) * std::cos(dlon);
    return normalize_degrees(rad_to_deg(static_cast<float>(std::atan2(y, x))));
}

double haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kEarthRadiusM = 6371000.0;
    const double dlat = deg_to_rad(static_cast<float>(lat2 - lat1));
    const double dlon = deg_to_rad(static_cast<float>(lon2 - lon1));
    const double a = std::pow(std::sin(dlat / 2.0), 2.0) +
                     std::cos(deg_to_rad(static_cast<float>(lat1))) *
                         std::cos(deg_to_rad(static_cast<float>(lat2))) *
                         std::pow(std::sin(dlon / 2.0), 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return kEarthRadiusM * c;
}

const char* compass_rose(float bearing)
{
    static constexpr const char* kNames[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const int index = static_cast<int>(std::floor((normalize_degrees(bearing) + 22.5f) / 45.0f)) % 8;
    return kNames[index];
}

} // namespace ui::menu::dashboard
