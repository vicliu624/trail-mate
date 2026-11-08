#include "ui/menu/dashboard/dashboard_widgets.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/app_facade_access.h"
#include "chat/domain/contact_types.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/orientation_runtime.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "ui/localization.h"
#include "ui/menu/dashboard/dashboard_style.h"

#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif

namespace ui::menu::dashboard
{
namespace
{

struct CompassWidgetUi : WidgetLifetime
{
    bool heading_lease = false;
    ~CompassWidgetUi()
    {
        if (heading_lease) platform::ui::orientation::release_heading();
    }
    DashboardCardChrome chrome{};
    lv_obj_t* dial = nullptr;
    lv_obj_t* dial_ring = nullptr;
    lv_obj_t* dial_inner_ring = nullptr;
    lv_obj_t* north_marker = nullptr;
    lv_obj_t* needle = nullptr;
    lv_point_precise_t needle_points[2]{};
    float displayed_bearing = -1.0f;
    lv_obj_t* center_dot = nullptr;
    lv_obj_t* cardinal_labels[4]{};
    lv_obj_t* target_name = nullptr;
    lv_obj_t* distance_value = nullptr;
    lv_obj_t* bearing_value = nullptr;
    lv_obj_t* footer_label = nullptr;
};

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

std::string best_name_for(const chat::contacts::PeerDirectoryItem& info)
{
    if (!info.display_name.empty())
    {
        return info.display_name;
    }
    if (info.short_name[0] != '\0')
    {
        return info.short_name;
    }
    return ::ui::i18n::tr("Unknown");
}

void update_needle(CompassWidgetUi& compass, float bearing)
{
    if (compass.displayed_bearing == bearing) return;
    compass.displayed_bearing = bearing;
    const float center = static_cast<float>(lv_obj_get_content_width(compass.dial)) / 2.0f;
    const float radius = std::max(1.0f, center - 12.0f);
    const float radians = bearing * 3.14159265358979323846f / 180.0f;
    compass.needle_points[0] = {static_cast<lv_value_precise_t>(center), static_cast<lv_value_precise_t>(center)};
    compass.needle_points[1] = {static_cast<lv_value_precise_t>(center + std::sin(radians) * radius),
                                static_cast<lv_value_precise_t>(center - std::cos(radians) * radius)};
    // Retain points in widget-owned state. A rotated object requires a temporary
    // compositing layer; a line renders directly without that allocation.
    lv_line_set_points(compass.needle, compass.needle_points, 2);
}

} // namespace

void refresh_compass_widget(void* context);

WidgetHandle create_compass_widget(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h)
{
    if (!platform::ui::orientation::heading_supported()) return {};
    auto* memory = create_widget_state<CompassWidgetUi>();
    if (!memory) return {};
    auto& compass = *memory;
    compass.heading_lease = platform::ui::orientation::acquire_heading();
    compass.chrome = create_card_chrome(parent, ::ui::i18n::tr("Mini Compass"), card_w, card_h);

    const lv_coord_t body_w = lv_obj_get_content_width(compass.chrome.body);
    const lv_coord_t body_h = lv_obj_get_content_height(compass.chrome.body);
    const lv_coord_t dial_d = std::min<lv_coord_t>(body_h - 8, std::max<lv_coord_t>(80, body_w / 2));

    compass.dial = lv_obj_create(compass.chrome.body);
    lv_obj_set_size(compass.dial, dial_d, dial_d);
    lv_obj_align(compass.dial, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_bg_color(compass.dial, lv_color_hex(0xF7ECD2), 0);
    lv_obj_set_style_bg_opa(compass.dial, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(compass.dial, 0, 0);
    lv_obj_set_style_radius(compass.dial, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(compass.dial, LV_OBJ_FLAG_SCROLLABLE);

    compass.dial_ring = lv_obj_create(compass.dial);
    style_ring_object(compass.dial_ring, dial_d - 4, 2, color_line());
    lv_obj_center(compass.dial_ring);

    compass.dial_inner_ring = lv_obj_create(compass.dial);
    style_ring_object(compass.dial_inner_ring, dial_d - 28, 1, color_line());
    lv_obj_center(compass.dial_inner_ring);

    compass.north_marker = lv_obj_create(compass.dial);
    lv_obj_set_size(compass.north_marker, 12, 12);
    lv_obj_set_style_radius(compass.north_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(compass.north_marker, color_warn(), 0);
    lv_obj_set_style_bg_opa(compass.north_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(compass.north_marker, 0, 0);
    lv_obj_align(compass.north_marker, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_update_layout(compass.dial);
    compass.needle = lv_line_create(compass.dial);
    lv_obj_set_pos(compass.needle, 0, 0);
    lv_obj_set_style_line_width(compass.needle, 4, 0);
    lv_obj_set_style_line_color(compass.needle, color_amber_dark(), 0);
    lv_obj_set_style_line_rounded(compass.needle, true, 0);
    update_needle(compass, 0.0f);

    compass.center_dot = lv_obj_create(compass.dial);
    lv_obj_set_size(compass.center_dot, 14, 14);
    lv_obj_set_style_radius(compass.center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(compass.center_dot, color_text(), 0);
    lv_obj_set_style_bg_opa(compass.center_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(compass.center_dot, 0, 0);
    lv_obj_center(compass.center_dot);

    static constexpr const char* kCardinals[] = {"N", "E", "S", "W"};
    static constexpr lv_align_t kAligns[] = {LV_ALIGN_TOP_MID, LV_ALIGN_RIGHT_MID, LV_ALIGN_BOTTOM_MID, LV_ALIGN_LEFT_MID};
    static constexpr lv_coord_t kOffsets[4][2] = {{0, -2}, {-3, 0}, {0, 2}, {3, 0}};
    for (size_t i = 0; i < 4; ++i)
    {
        compass.cardinal_labels[i] = lv_label_create(compass.dial);
        style_body_label(compass.cardinal_labels[i], &lv_font_montserrat_12, i == 0 ? color_warn() : color_text_dim());
        lv_label_set_text(compass.cardinal_labels[i], kCardinals[i]);
        lv_obj_align(compass.cardinal_labels[i], kAligns[i], kOffsets[i][0], kOffsets[i][1]);
    }

    const lv_coord_t stats_x = static_cast<lv_coord_t>(dial_d + 14);
    const lv_coord_t stats_w = std::max<lv_coord_t>(96, body_w - stats_x);

    compass.target_name = lv_label_create(compass.chrome.body);
    style_body_label(compass.target_name, &lv_font_montserrat_16, color_text());
    lv_obj_set_width(compass.target_name, stats_w);
    lv_obj_set_pos(compass.target_name, stats_x, 4);
    ::ui::i18n::set_label_text(compass.target_name, "No target");

    compass.distance_value = lv_label_create(compass.chrome.body);
    style_body_label(compass.distance_value, &lv_font_montserrat_16, color_info());
    lv_obj_set_pos(compass.distance_value, stats_x, 34);
    ::ui::i18n::set_label_text_raw(compass.distance_value, "--");

    compass.bearing_value = lv_label_create(compass.chrome.body);
    style_body_label(compass.bearing_value, &lv_font_montserrat_12, color_text_dim());
    lv_obj_set_width(compass.bearing_value, stats_w);
    lv_obj_set_pos(compass.bearing_value, stats_x, 62);
    ::ui::i18n::set_label_text(compass.bearing_value, "awaiting position");

    compass.footer_label = lv_label_create(compass.chrome.footer);
    style_footer_label(compass.footer_label);
    lv_obj_align(compass.footer_label, LV_ALIGN_LEFT_MID, 0, 0);
    ::ui::i18n::set_label_text(compass.footer_label, "compass wakes after GPS fix");
    return own_widget(compass.chrome.card, memory, refresh_compass_widget);
}

void refresh_compass_widget(void* context)
{
    auto& compass = *static_cast<CompassWidgetUi*>(context);
    if (compass.chrome.card == nullptr)
    {
        return;
    }

    const auto self = platform::ui::gps::get_data();
    const auto heading = platform::ui::orientation::get_heading();
    const bool has_heading_sensor = platform::ui::orientation::heading_supported();
    const bool self_valid = self.valid;

    double best_distance = 0.0;
    float best_bearing = 0.0f;
    float needle_bearing = 0.0f;
    std::string best_name = "No target";
    bool found_target = false;

    if (self_valid)
    {
        team::ui::TeamUiSnapshot team_snap;
        if (team::ui::team_ui_snapshot_store().load(team_snap))
        {
            auto& contacts = app::messagingFacade().getContactService();
            const uint32_t self_id = app::messagingFacade().getSelfNodeId();
            for (const auto& member : team_snap.members)
            {
                if (member.node_id == 0 || member.node_id == self_id)
                {
                    continue;
                }

                const chat::contacts::PeerDirectoryItem* info = contacts.getPeerByNodeId(member.node_id);
                if (info == nullptr || !info->position.valid)
                {
                    continue;
                }

                const double lat = static_cast<double>(info->position.latitude_i) / 1e7;
                const double lon = static_cast<double>(info->position.longitude_i) / 1e7;
                const double distance = haversine_m(self.lat, self.lng, lat, lon);
                if (!found_target || distance < best_distance)
                {
                    found_target = true;
                    best_distance = distance;
                    best_bearing = bearing_between(self.lat, self.lng, lat, lon);
                    best_name = best_name_for(*info);
                }
            }
        }
    }

    if (!self_valid)
    {
        set_status_chip(compass.chrome,
                        heading.available ? "COMPASS" : "NO FIX",
                        heading.available ? color_soft_blue() : color_soft_warn(),
                        heading.available ? color_info() : color_warn());
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        set_label_text_if_changed(compass.target_name, heading.available ? "Magnetic north" : "Need local GPS");
#else
        set_label_text_if_changed(compass.target_name, heading.available ? "North locked" : "Need local GPS");
#endif

        char heading_buf[48];
        if (heading.available)
        {
            std::snprintf(heading_buf, sizeof(heading_buf), "%s  |  %03.0f deg", compass_rose(heading.heading_deg), heading.heading_deg);
            needle_bearing = normalize_degrees(360.0f - heading.heading_deg);
        }
        else
        {
            std::snprintf(heading_buf, sizeof(heading_buf), "--");
        }
        set_label_text_if_changed_raw(compass.distance_value, heading.available ? heading_buf : "--");
        set_label_text_if_changed(compass.bearing_value,
                                  heading.available ? "magnetometer heading active" : "heading unlocks after first fix");
        set_label_text_if_changed(compass.footer_label,
                                  heading.available ? "device heading from 9-axis module" : "compass wakes after GPS fix");
#if defined(TRAIL_MATE_CARDPUTER_ZERO_LINUX)
        if (heading.available) set_label_text_if_changed(compass.footer_label, "Hold level | uncalibrated magnetic heading");
#endif
        update_needle(compass, needle_bearing);
        lv_obj_set_style_bg_opa(compass.north_marker, LV_OPA_COVER, 0);
        return;
    }

    if (!found_target)
    {
        set_status_chip(compass.chrome,
                        heading.available || !has_heading_sensor ? "SEARCH" : "WAIT IMU",
                        color_soft_blue(),
                        color_info());
        set_label_text_if_changed(compass.target_name, heading.available || !has_heading_sensor ? "Awaiting teammate" : "Heading starting");
        if (heading.available)
        {
            char heading_buf[48];
            std::snprintf(heading_buf, sizeof(heading_buf), "%s  |  %03.0f deg", compass_rose(heading.heading_deg), heading.heading_deg);
            set_label_text_if_changed_raw(compass.distance_value, heading_buf);
            set_label_text_if_changed(compass.bearing_value, "team member position will appear here");
            set_label_text_if_changed(compass.footer_label, "needle shows north until target lock");
            needle_bearing = normalize_degrees(360.0f - heading.heading_deg);
        }
        else
        {
            set_label_text_if_changed_raw(compass.distance_value, "--");
            set_label_text_if_changed(compass.bearing_value, has_heading_sensor ? "magnetometer still warming up" : "team member position will appear here");
            set_label_text_if_changed(compass.footer_label, has_heading_sensor ? "waiting for module GNSS compass" : "GPS bearings to nearby teammates");
        }
        update_needle(compass, needle_bearing);
        lv_obj_set_style_bg_opa(compass.north_marker, LV_OPA_COVER, 0);
        return;
    }

    set_status_chip(compass.chrome, compass_rose(best_bearing), color_soft_green(), color_ok());
    set_label_text_if_changed_raw(compass.target_name, best_name.c_str());

    char distance_buf[24];
    format_distance(best_distance, distance_buf, sizeof(distance_buf));
    set_label_text_if_changed_raw(compass.distance_value, distance_buf);

    char bearing_buf[48];
    if (heading.available)
    {
        std::snprintf(bearing_buf,
                      sizeof(bearing_buf),
                      "%s  |  %03.0f tgt  /  %03.0f hdg",
                      compass_rose(best_bearing),
                      best_bearing,
                      heading.heading_deg);
        needle_bearing = normalize_degrees(best_bearing - heading.heading_deg);
    }
    else
    {
        std::snprintf(bearing_buf, sizeof(bearing_buf), "%s  |  %03.0f deg", compass_rose(best_bearing), best_bearing);
        needle_bearing = best_bearing;
    }
    set_label_text_if_changed_raw(compass.bearing_value, bearing_buf);

    char footer[64];
    if (heading.available)
    {
        std::snprintf(
            footer, sizeof(footer), "%s", ::ui::i18n::format("peer lock  |  turn %03.0f deg", needle_bearing).c_str());
    }
    else
    {
        std::snprintf(
            footer, sizeof(footer), "%s", ::ui::i18n::format("nearest peer  |  %.0f m away", best_distance).c_str());
    }
    set_label_text_if_changed_raw(compass.footer_label, footer);

    update_needle(compass, needle_bearing);
    lv_obj_set_style_bg_opa(compass.north_marker, LV_OPA_COVER, 0);
}

} // namespace ui::menu::dashboard
