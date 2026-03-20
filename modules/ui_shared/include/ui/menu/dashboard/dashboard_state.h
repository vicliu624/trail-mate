#pragma once

#include "ui/menu/dashboard/dashboard_style.h"

namespace ui::menu::dashboard
{

struct MeshWidgetUi
{
    DashboardCardChrome chrome{};
    lv_obj_t* orbit_outer = nullptr;
    lv_obj_t* orbit_mid = nullptr;
    lv_obj_t* orbit_core = nullptr;
    lv_obj_t* hero_value = nullptr;
    lv_obj_t* hero_caption = nullptr;
    lv_obj_t* stat_tiles[3]{};
    lv_obj_t* stat_values[3]{};
    lv_obj_t* stat_captions[3]{};
    lv_obj_t* signal_bar_wrap = nullptr;
    lv_obj_t* signal_bars[12]{};
    lv_obj_t* footer_label = nullptr;
};

struct GpsWidgetUi
{
    DashboardCardChrome chrome{};
    lv_obj_t* radar = nullptr;
    lv_obj_t* radar_rings[3]{};
    lv_obj_t* radar_cross_h = nullptr;
    lv_obj_t* radar_cross_v = nullptr;
    lv_obj_t* sat_dots[8]{};
    lv_obj_t* sat_labels[8]{};
    lv_obj_t* sat_use_tags[8]{};
    lv_obj_t* stat_tiles[3]{};
    lv_obj_t* stat_values[3]{};
    lv_obj_t* stat_captions[3]{};
    lv_obj_t* footer_label = nullptr;
};

struct RecentWidgetUi
{
    DashboardCardChrome chrome{};
    lv_obj_t* rows[2]{};
    lv_obj_t* row_dots[2]{};
    lv_obj_t* row_names[2]{};
    lv_obj_t* row_previews[2]{};
    lv_obj_t* row_badges[2]{};
    lv_obj_t* row_badge_labels[2]{};
    lv_obj_t* scan_track = nullptr;
    lv_obj_t* scan_runner = nullptr;
    lv_obj_t* footer_label = nullptr;
};

struct CompassWidgetUi
{
    DashboardCardChrome chrome{};
    lv_obj_t* dial = nullptr;
    lv_obj_t* dial_ring = nullptr;
    lv_obj_t* dial_inner_ring = nullptr;
    lv_obj_t* north_marker = nullptr;
    lv_obj_t* needle = nullptr;
    lv_obj_t* center_dot = nullptr;
    lv_obj_t* cardinal_labels[4]{};
    lv_obj_t* target_name = nullptr;
    lv_obj_t* distance_value = nullptr;
    lv_obj_t* bearing_value = nullptr;
    lv_obj_t* footer_label = nullptr;
};

struct DashboardUi
{
    lv_obj_t* panel = nullptr;
    lv_obj_t* grid = nullptr;
    lv_timer_t* timer = nullptr;
    uint32_t tick = 0;
    bool dock_right = false;
    MeshWidgetUi mesh{};
    GpsWidgetUi gps{};
    RecentWidgetUi recent{};
    CompassWidgetUi compass{};
};

DashboardUi& dashboard_state();
void dashboard_reset();

} // namespace ui::menu::dashboard
