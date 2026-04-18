#include "ui/menu/dashboard/dashboard_widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "platform/ui/gps_runtime.h"
#include "sys/clock.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_state.h"

namespace ui::menu::dashboard
{
namespace
{

void logDashboardGpsFlow(bool has_snapshot, std::size_t sat_count, const gps::GnssStatus& gnss, bool fix)
{
    static bool s_last_has_snapshot = false;
    static bool s_last_fix = false;
    static std::size_t s_last_sat_count = static_cast<std::size_t>(-1);
    static uint8_t s_last_sats_in_view = 0xFF;
    static uint8_t s_last_sats_in_use = 0xFF;
    static uint32_t s_last_log_ms = 0;

    const uint32_t now_ms = sys::millis_now();
    const bool changed = (has_snapshot != s_last_has_snapshot) || (fix != s_last_fix) || (sat_count != s_last_sat_count) ||
                         (gnss.sats_in_view != s_last_sats_in_view) || (gnss.sats_in_use != s_last_sats_in_use);
    const bool suspicious_full_scale =
        has_snapshot && sat_count == gps::kMaxGnssSats && gnss.sats_in_view != static_cast<uint8_t>(sat_count);
    if (!changed && !suspicious_full_scale && (now_ms - s_last_log_ms) < 2000U)
    {
        return;
    }

    s_last_has_snapshot = has_snapshot;
    s_last_fix = fix;
    s_last_sat_count = sat_count;
    s_last_sats_in_view = gnss.sats_in_view;
    s_last_sats_in_use = gnss.sats_in_use;
    s_last_log_ms = now_ms;

    std::printf("[ui][gps][dashboard] has=%u fix=%u count=%u used=%u view=%u hdop=%.1f\n",
                static_cast<unsigned>(has_snapshot ? 1 : 0),
                static_cast<unsigned>(fix ? 1 : 0),
                static_cast<unsigned>(sat_count),
                static_cast<unsigned>(gnss.sats_in_use),
                static_cast<unsigned>(gnss.sats_in_view),
                static_cast<double>(gnss.hdop));
}

void set_label_text_if_changed_raw(lv_obj_t* label, const char* text)
{
    if (label == nullptr || text == nullptr)
    {
        return;
    }
    const char* current = lv_label_get_text(label);
    if (current == nullptr || std::strcmp(current, text) != 0)
    {
        ::ui::i18n::set_label_text_raw(label, text);
    }
}

void set_label_text_if_changed(lv_obj_t* label, const char* english)
{
    const char* localized = ::ui::i18n::tr(english);
    if (label == nullptr || localized == nullptr)
    {
        return;
    }
    const char* current = lv_label_get_text(label);
    if (current == nullptr || std::strcmp(current, localized) != 0)
    {
        ::ui::i18n::set_label_text_raw(label, localized);
    }
}

lv_color_t satellite_color(gps::GnssSystem sys)
{
    switch (sys)
    {
    case gps::GnssSystem::GPS:
        return color_amber_dark();
    case gps::GnssSystem::GLN:
        return color_info();
    case gps::GnssSystem::GAL:
        return color_ok();
    case gps::GnssSystem::BD:
        return color_warn();
    default:
        return color_text_dim();
    }
}

int sat_rank(const gps::GnssSatInfo& sat)
{
    const int used_bonus = sat.used ? 1000 : 0;
    const int snr_score = std::max(0, static_cast<int>(sat.snr)) * 10;
    return used_bonus + snr_score + sat.elevation;
}

const char* fix_text(gps::GnssFix fix)
{
    switch (fix)
    {
    case gps::GnssFix::FIX3D:
        return "3D FIX";
    case gps::GnssFix::FIX2D:
        return "2D FIX";
    default:
        return "NO FIX";
    }
}

void create_stat_tiles(GpsWidgetUi& gps_ui, lv_obj_t* body, lv_coord_t x, lv_coord_t tile_w)
{
    static constexpr const char* kCaptions[] = {"Fix", "Used", "HDOP"};
    const lv_color_t colors[] = {color_soft_blue(), color_soft_green(), color_soft_amber()};

    for (size_t i = 0; i < 3; ++i)
    {
        gps_ui.stat_tiles[i] = lv_obj_create(body);
        lv_obj_set_size(gps_ui.stat_tiles[i], tile_w, 30);
        lv_obj_set_pos(gps_ui.stat_tiles[i], x, static_cast<lv_coord_t>(i * 34));
        style_stat_tile(gps_ui.stat_tiles[i], colors[i]);

        gps_ui.stat_values[i] = lv_label_create(gps_ui.stat_tiles[i]);
        style_body_label(gps_ui.stat_values[i], &lv_font_montserrat_16, color_text());
        lv_obj_align(gps_ui.stat_values[i], LV_ALIGN_LEFT_MID, 0, -1);
        ::ui::i18n::set_label_text_raw(gps_ui.stat_values[i], "--");

        gps_ui.stat_captions[i] = lv_label_create(gps_ui.stat_tiles[i]);
        style_body_label(gps_ui.stat_captions[i], &lv_font_montserrat_12, color_text_dim());
        lv_obj_align(gps_ui.stat_captions[i], LV_ALIGN_RIGHT_MID, 0, -1);
        ::ui::i18n::set_label_text(gps_ui.stat_captions[i], kCaptions[i]);
    }
}

} // namespace

void create_gps_widget(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h)
{
    auto& gps_ui = dashboard_state().gps;
    gps_ui.chrome = create_card_chrome(parent, ::ui::i18n::tr("GPS Status"), card_w, card_h);

    const lv_coord_t body_w = lv_obj_get_width(gps_ui.chrome.body);
    const lv_coord_t body_h = lv_obj_get_height(gps_ui.chrome.body);
    const lv_coord_t radar_d = std::min<lv_coord_t>(body_h - 8, std::max<lv_coord_t>(74, body_w / 2));

    gps_ui.radar = lv_obj_create(gps_ui.chrome.body);
    lv_obj_set_size(gps_ui.radar, radar_d, radar_d);
    lv_obj_align(gps_ui.radar, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(gps_ui.radar, lv_color_hex(0xF7ECD2), 0);
    lv_obj_set_style_bg_opa(gps_ui.radar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gps_ui.radar, 0, 0);
    lv_obj_set_style_radius(gps_ui.radar, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(gps_ui.radar, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < 3; ++i)
    {
        gps_ui.radar_rings[i] = lv_obj_create(gps_ui.radar);
        style_ring_object(gps_ui.radar_rings[i],
                          static_cast<lv_coord_t>(radar_d - static_cast<lv_coord_t>(i * (radar_d / 4))),
                          1,
                          color_line());
        lv_obj_center(gps_ui.radar_rings[i]);
    }

    gps_ui.radar_cross_h = lv_obj_create(gps_ui.radar);
    lv_obj_set_size(gps_ui.radar_cross_h, radar_d - 16, 1);
    lv_obj_center(gps_ui.radar_cross_h);
    lv_obj_set_style_bg_color(gps_ui.radar_cross_h, color_line(), 0);
    lv_obj_set_style_bg_opa(gps_ui.radar_cross_h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gps_ui.radar_cross_h, 0, 0);

    gps_ui.radar_cross_v = lv_obj_create(gps_ui.radar);
    lv_obj_set_size(gps_ui.radar_cross_v, 1, radar_d - 16);
    lv_obj_center(gps_ui.radar_cross_v);
    lv_obj_set_style_bg_color(gps_ui.radar_cross_v, color_line(), 0);
    lv_obj_set_style_bg_opa(gps_ui.radar_cross_v, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gps_ui.radar_cross_v, 0, 0);

    for (size_t i = 0; i < 8; ++i)
    {
        gps_ui.sat_dots[i] = lv_obj_create(gps_ui.radar);
        lv_obj_set_size(gps_ui.sat_dots[i], 16, 16);
        lv_obj_set_style_radius(gps_ui.sat_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(gps_ui.sat_dots[i], color_line(), 0);
        lv_obj_set_style_bg_opa(gps_ui.sat_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(gps_ui.sat_dots[i], 2, 0);
        lv_obj_set_style_border_color(gps_ui.sat_dots[i], color_text_dim(), 0);
        lv_obj_add_flag(gps_ui.sat_dots[i], LV_OBJ_FLAG_HIDDEN);

        gps_ui.sat_labels[i] = lv_label_create(gps_ui.sat_dots[i]);
        style_body_label(gps_ui.sat_labels[i], &lv_font_montserrat_12, lv_color_white());
        lv_obj_center(gps_ui.sat_labels[i]);

        gps_ui.sat_use_tags[i] = lv_label_create(gps_ui.radar);
        lv_obj_set_style_bg_color(gps_ui.sat_use_tags[i], color_ok(), 0);
        lv_obj_set_style_bg_opa(gps_ui.sat_use_tags[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(gps_ui.sat_use_tags[i], 6, 0);
        lv_obj_set_style_pad_left(gps_ui.sat_use_tags[i], 4, 0);
        lv_obj_set_style_pad_right(gps_ui.sat_use_tags[i], 4, 0);
        lv_obj_set_style_pad_top(gps_ui.sat_use_tags[i], 1, 0);
        lv_obj_set_style_pad_bottom(gps_ui.sat_use_tags[i], 1, 0);
        lv_obj_set_style_text_color(gps_ui.sat_use_tags[i], lv_color_white(), 0);
        lv_obj_set_style_text_font(gps_ui.sat_use_tags[i], &lv_font_montserrat_12, 0);
        ::ui::i18n::set_label_text(gps_ui.sat_use_tags[i], "U");
        lv_obj_add_flag(gps_ui.sat_use_tags[i], LV_OBJ_FLAG_HIDDEN);
    }

    create_stat_tiles(gps_ui, gps_ui.chrome.body, static_cast<lv_coord_t>(radar_d + 14),
                      std::max<lv_coord_t>(96, body_w - radar_d - 18));

    gps_ui.footer_label = lv_label_create(gps_ui.chrome.footer);
    style_footer_label(gps_ui.footer_label);
    lv_obj_align(gps_ui.footer_label, LV_ALIGN_LEFT_MID, 0, 0);
    ::ui::i18n::set_label_text(gps_ui.footer_label, "searching satellites");
}

void refresh_gps_widget()
{
    auto& gps_ui = dashboard_state().gps;
    if (gps_ui.chrome.card == nullptr)
    {
        return;
    }

    gps::GnssSatInfo sats[gps::kMaxGnssSats];
    std::size_t sat_count = 0;
    gps::GnssStatus gnss{};
    const bool has_snapshot = platform::ui::gps::get_gnss_snapshot(sats, gps::kMaxGnssSats, &sat_count, &gnss);
    const auto state = platform::ui::gps::get_data();
    const bool fix = state.valid || (has_snapshot && gnss.fix != gps::GnssFix::NOFIX);
    logDashboardGpsFlow(has_snapshot, sat_count, gnss, fix);

    set_status_chip(gps_ui.chrome,
                    fix_text(gnss.fix),
                    fix ? color_soft_green() : color_soft_warn(),
                    fix ? color_ok() : color_warn());

    set_label_text_if_changed(gps_ui.stat_values[0], fix_text(gnss.fix));

    char used_buf[16];
    std::snprintf(used_buf, sizeof(used_buf), "%u/%u",
                  static_cast<unsigned>(has_snapshot ? gnss.sats_in_use : 0),
                  static_cast<unsigned>(has_snapshot ? sat_count : 0));
    set_label_text_if_changed_raw(gps_ui.stat_values[1], used_buf);

    char hdop_buf[16];
    std::snprintf(hdop_buf, sizeof(hdop_buf), "%.1f", has_snapshot ? gnss.hdop : 0.0f);
    set_label_text_if_changed_raw(gps_ui.stat_values[2], hdop_buf);

    std::array<gps::GnssSatInfo, gps::kMaxGnssSats> ranked{};
    for (size_t i = 0; i < sat_count && i < ranked.size(); ++i)
    {
        ranked[i] = sats[i];
    }
    std::sort(ranked.begin(),
              ranked.begin() + static_cast<std::ptrdiff_t>(std::min(sat_count, ranked.size())),
              [](const gps::GnssSatInfo& lhs, const gps::GnssSatInfo& rhs)
              {
                  const int lhs_rank = sat_rank(lhs);
                  const int rhs_rank = sat_rank(rhs);
                  if (lhs_rank != rhs_rank)
                  {
                      return lhs_rank > rhs_rank;
                  }
                  return lhs.id < rhs.id;
              });

    const lv_coord_t radar_w = lv_obj_get_width(gps_ui.radar);
    const lv_coord_t center = radar_w / 2;
    const float radius = static_cast<float>((radar_w / 2) - 12);
    const size_t shown = std::min<std::size_t>(8, sat_count);
    for (size_t i = 0; i < 8; ++i)
    {
        const bool visible = i < shown;
        if (!visible)
        {
            lv_obj_add_flag(gps_ui.sat_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(gps_ui.sat_use_tags[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const auto& sat = ranked[i];
        const float sat_r = radius * (1.0f - (static_cast<float>(sat.elevation) / 90.0f));
        const float angle = normalize_degrees(static_cast<float>(sat.azimuth));
        const float radians = angle * 3.14159265358979323846f / 180.0f;
        const lv_coord_t x = static_cast<lv_coord_t>(center + sat_r * std::sin(radians) - 8.0f);
        const lv_coord_t y = static_cast<lv_coord_t>(center - sat_r * std::cos(radians) - 8.0f);

        lv_obj_clear_flag(gps_ui.sat_dots[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(gps_ui.sat_dots[i], x, y);
        lv_obj_set_style_bg_color(gps_ui.sat_dots[i], satellite_color(sat.sys), 0);
        lv_obj_set_style_border_color(gps_ui.sat_dots[i], sat.used ? color_ok() : (sat.snr >= 20 ? color_info() : color_warn()), 0);

        char label_buf[8];
        std::snprintf(label_buf, sizeof(label_buf), "%u", static_cast<unsigned>(sat.id));
        set_label_text_if_changed_raw(gps_ui.sat_labels[i], label_buf);

        if (sat.used)
        {
            lv_obj_clear_flag(gps_ui.sat_use_tags[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(gps_ui.sat_use_tags[i], static_cast<lv_coord_t>(x - 4), static_cast<lv_coord_t>(y + 12));
        }
        else
        {
            lv_obj_add_flag(gps_ui.sat_use_tags[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (size_t i = 0; i < 3; ++i)
    {
        lv_obj_set_style_border_opa(gps_ui.radar_rings[i], i == 0 ? LV_OPA_COVER : LV_OPA_60, 0);
        lv_obj_set_style_border_color(gps_ui.radar_rings[i], i == 0 ? color_info() : color_line(), 0);
    }

    char footer[72];
    std::snprintf(footer,
                  sizeof(footer),
                  "%s",
                  ::ui::i18n::format(
                      fix ? "position stream active  |  %u in view" : "waiting for better sky view  |  %u in view",
                      static_cast<unsigned>(has_snapshot ? sat_count : 0))
                      .c_str());
    set_label_text_if_changed_raw(gps_ui.footer_label, footer);
}

} // namespace ui::menu::dashboard
