#include "gps_tracker_overlay.h"

#include "../../ui_common.h"
#include "../../widgets/map/map_tiles.h"
#include "gps_modal.h"
#include "gps_page_lifetime.h"
#include "gps_page_map.h"
#include "gps_page_styles.h"
#include "gps_state.h"

#include <SD.h>
#include <algorithm>
#include <cmath>
#include <vector>

// Implemented in gps_page_components.cpp
extern void show_toast(const char* message, uint32_t duration_ms);

namespace
{
using gps::ui::lifetime::is_alive;

constexpr double kMinDistanceM = 2.0;
constexpr int kMaxDrawPoints = 150;
constexpr int kDefaultTrackerZoom = 16;
constexpr uint32_t kTrackColor = 0xFF2D55; // vivid pink/red, uncommon on map tiles
std::vector<String> s_modal_names;

double deg2rad(double deg)
{
    return deg * 0.017453292519943295; // pi / 180
}

double haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double R = 6371000.0;
    const double dlat = deg2rad(lat2 - lat1);
    const double dlon = deg2rad(lon2 - lon1);
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(deg2rad(lat1)) * std::cos(deg2rad(lat2)) *
                         std::sin(dlon / 2) * std::sin(dlon / 2);
    const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return R * c;
}

double sampling_distance_m()
{
    return kMinDistanceM;
}

bool parse_attr_double(const String& line, const char* key, double& out)
{
    const String token = String(key) + "=\"";
    const int start = line.indexOf(token);
    if (start < 0)
    {
        return false;
    }
    const int val_start = start + token.length();
    const int val_end = line.indexOf('"', val_start);
    if (val_end <= val_start)
    {
        return false;
    }
    out = line.substring(val_start, val_end).toDouble();
    return true;
}

void downsample_points(std::vector<GPSPageState::TrackOverlayPoint>& points)
{
    if (points.size() <= (size_t)kMaxDrawPoints)
    {
        return;
    }
    const size_t total = points.size();
    std::vector<GPSPageState::TrackOverlayPoint> reduced;
    reduced.reserve(kMaxDrawPoints);
    for (size_t i = 0; i < (size_t)kMaxDrawPoints; ++i)
    {
        const size_t idx = (i * (total - 1)) / (kMaxDrawPoints - 1);
        reduced.push_back(points[idx]);
    }
    points.swap(reduced);
}

bool load_gpx_points(const char* path, std::vector<GPSPageState::TrackOverlayPoint>& out_points)
{
    out_points.clear();
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }

    File f = SD.open(path, FILE_READ);
    if (!f)
    {
        return false;
    }

    bool have_last = false;
    GPSPageState::TrackOverlayPoint last{};

    while (f.available())
    {
        const String line = f.readStringUntil('\n');
        if (line.indexOf("<trkpt") < 0)
        {
            continue;
        }
        double lat = 0.0;
        double lon = 0.0;
        if (!parse_attr_double(line, "lat", lat))
        {
            continue;
        }
        if (!parse_attr_double(line, "lon", lon))
        {
            continue;
        }
        GPSPageState::TrackOverlayPoint pt;
        pt.lat = lat;
        pt.lng = lon;

        if (have_last)
        {
            const double min_d = sampling_distance_m();
            const double d = haversine_m(last.lat, last.lng, pt.lat, pt.lng);
            if (d < min_d)
            {
                continue;
            }
        }

        out_points.push_back(pt);
        downsample_points(out_points);

        last = pt;
        have_last = true;
    }

    f.close();
    return !out_points.empty();
}

bool load_csv_points(const char* path, std::vector<GPSPageState::TrackOverlayPoint>& out_points)
{
    out_points.clear();
    if (SD.cardType() == CARD_NONE)
    {
        return false;
    }

    File f = SD.open(path, FILE_READ);
    if (!f)
    {
        return false;
    }

    bool have_last = false;
    GPSPageState::TrackOverlayPoint last{};

    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
        {
            continue;
        }
        char c0 = line[0];
        if (!((c0 >= '0' && c0 <= '9') || c0 == '-'))
        {
            continue;
        }

        int comma1 = line.indexOf(',');
        if (comma1 <= 0)
        {
            continue;
        }
        int comma2 = line.indexOf(',', comma1 + 1);
        String lat_str = line.substring(0, comma1);
        String lon_str = (comma2 > comma1) ? line.substring(comma1 + 1, comma2)
                                           : line.substring(comma1 + 1);

        double lat = lat_str.toDouble();
        double lon = lon_str.toDouble();

        GPSPageState::TrackOverlayPoint pt;
        pt.lat = lat;
        pt.lng = lon;

        if (have_last)
        {
            const double min_d = sampling_distance_m();
            const double d = haversine_m(last.lat, last.lng, pt.lat, pt.lng);
            if (d < min_d)
            {
                continue;
            }
        }

        out_points.push_back(pt);
        downsample_points(out_points);

        last = pt;
        have_last = true;
    }

    f.close();
    return !out_points.empty();
}

void compute_screen_points()
{
    if (!is_alive())
    {
        return;
    }
    auto& s = g_gps_state;
    s.tracker_screen_points.clear();

    if (!s.tracker_overlay_active || s.tracker_points.empty())
    {
        return;
    }
    if (!s.tile_ctx.anchor || !s.tile_ctx.anchor->valid || !s.map)
    {
        return;
    }

    const int w = lv_obj_get_width(s.map);
    const int h = lv_obj_get_height(s.map);
    if (w <= 0 || h <= 0)
    {
        return;
    }

    const int total = static_cast<int>(s.tracker_points.size());
    const int stride = std::max(1, total / kMaxDrawPoints);

    bool have_prev = false;
    GPSPageState::TrackOverlayPoint prev{};

    for (int i = total - 1; i >= 0 && (int)s.tracker_screen_points.size() < kMaxDrawPoints; i -= stride)
    {
        const auto& pt = s.tracker_points[(size_t)i];
        if (have_prev)
        {
            const double min_d = sampling_distance_m();
            const double d = haversine_m(prev.lat, prev.lng, pt.lat, pt.lng);
            if (d < min_d)
            {
                continue;
            }
        }

        double map_lat = 0.0;
        double map_lon = 0.0;
        gps_map_transform(pt.lat, pt.lng, map_lat, map_lon);
        int sx = 0;
        int sy = 0;
        if (!gps_screen_pos(s.tile_ctx, map_lat, map_lon, sx, sy))
        {
            break;
        }

        lv_point_t p;
        p.x = (lv_coord_t)sx;
        p.y = (lv_coord_t)sy;
        s.tracker_screen_points.push_back(p);

        prev = pt;
        have_prev = true;
    }
}

void apply_tracker_view_defaults()
{
    if (!is_alive())
    {
        return;
    }
    auto& s = g_gps_state;
    if (s.tracker_points.empty())
    {
        return;
    }

    const auto& last = s.tracker_points.back();
    s.zoom_level = kDefaultTrackerZoom;
    s.pan_x = 0;
    s.pan_y = 0;
    s.lat = last.lat;
    s.lng = last.lng;

    reset_title_status_cache();
    update_title_and_status();
    update_resolution_display();
    update_map_tiles(false);

    if (s.map)
    {
        lv_obj_invalidate(s.map);
    }
}

void close_tracker_modal()
{
    if (!is_alive())
    {
        return;
    }
    if (!modal_is_open(g_gps_state.tracker_modal))
    {
        return;
    }
    modal_close(g_gps_state.tracker_modal);
    bind_encoder_to_group(app_g);
}

void on_tracker_close_clicked(lv_event_t* /*e*/)
{
    close_tracker_modal();
}

void on_track_selected(lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    const uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
    if (idx >= s_modal_names.size())
    {
        return;
    }
    const String& name = s_modal_names[idx];

    char path[96];
    snprintf(path, sizeof(path), "/trackers/%s", name.c_str());

    if (!gps_tracker_load_file(path, true))
    {
        close_tracker_modal();
        return;
    }

    close_tracker_modal();
}

void build_tracker_modal()
{
    if (!is_alive())
    {
        return;
    }
    auto& s = g_gps_state;

    lv_obj_t* title = lv_label_create(s.tracker_modal.win);
    lv_label_set_text(title, "Select Track");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* list = lv_list_create(s.tracker_modal.win);
    lv_obj_set_size(list, LV_PCT(100), 90);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 28);
    gps::ui::styles::apply_tracker_modal_list(list);

    lv_obj_t* close_btn = lv_btn_create(s.tracker_modal.win);
    lv_obj_set_size(close_btn, 100, 24);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
    gps::ui::styles::apply_control_button(close_btn);
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, "Close");
    gps::ui::styles::apply_control_button_label(close_label);
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_btn, on_tracker_close_clicked, LV_EVENT_CLICKED, nullptr);

    File dir = SD.open("/trackers");
    if (!dir || !dir.isDirectory())
    {
        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, "No trackers folder");
        lv_group_remove_all_objs(s.tracker_modal.group);
        lv_group_add_obj(s.tracker_modal.group, close_btn);
        set_default_group(s.tracker_modal.group);
        bind_encoder_to_group(s.tracker_modal.group);
        lv_group_focus_obj(close_btn);
        return;
    }

    s_modal_names.clear();
    for (File f = dir.openNextFile(); f; f = dir.openNextFile())
    {
        if (!f.isDirectory())
        {
            s_modal_names.push_back(String(f.name()));
        }
        f.close();
    }
    dir.close();

    std::sort(s_modal_names.begin(), s_modal_names.end());
    if (s_modal_names.empty())
    {
        lv_obj_t* label = lv_label_create(list);
        lv_label_set_text(label, "No track files");
        lv_group_remove_all_objs(s.tracker_modal.group);
        lv_group_add_obj(s.tracker_modal.group, close_btn);
        set_default_group(s.tracker_modal.group);
        bind_encoder_to_group(s.tracker_modal.group);
        lv_group_focus_obj(close_btn);
        return;
    }

    lv_group_remove_all_objs(s.tracker_modal.group);
    for (size_t i = 0; i < s_modal_names.size(); ++i)
    {
        const String& n = s_modal_names[i];
        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_FILE, n.c_str());
        lv_obj_set_height(btn, 26);
        lv_obj_set_style_pad_ver(btn, 2, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(btn, 6, LV_PART_MAIN);
        gps::ui::styles::apply_control_button(btn);
        const uint32_t child_count = lv_obj_get_child_cnt(btn);
        for (uint32_t c = 0; c < child_count; ++c)
        {
            lv_obj_t* child = lv_obj_get_child(btn, c);
            if (child && lv_obj_check_type(child, &lv_label_class))
            {
                gps::ui::styles::apply_control_button_label(child);
            }
        }
        lv_obj_add_event_cb(btn, on_track_selected, LV_EVENT_CLICKED, (void*)i);
        lv_group_add_obj(s.tracker_modal.group, btn);
    }
    lv_group_add_obj(s.tracker_modal.group, close_btn);

    set_default_group(s.tracker_modal.group);
    bind_encoder_to_group(s.tracker_modal.group);
    if (lv_obj_t* first = lv_group_get_focused(s.tracker_modal.group))
    {
        lv_group_focus_obj(first);
    }
}

} // namespace

bool gps_tracker_load_file(const char* path, bool show_fail_toast)
{
    if (!is_alive() || !path)
    {
        return false;
    }

    std::vector<GPSPageState::TrackOverlayPoint> pts;
    bool ok = false;
    String p = String(path);
    p.toLowerCase();
    if (p.endsWith(".csv"))
    {
        ok = load_csv_points(path, pts);
    }
    else
    {
        ok = load_gpx_points(path, pts);
    }
    if (!ok)
    {
        if (show_fail_toast)
        {
            show_toast("No track yet", 1500);
        }
        g_gps_state.tracker_overlay_active = false;
        g_gps_state.tracker_points.clear();
        g_gps_state.tracker_screen_points.clear();
        g_gps_state.tracker_file.clear();
        return false;
    }

    g_gps_state.tracker_file = path;
    g_gps_state.tracker_points = std::move(pts);
    g_gps_state.tracker_overlay_active = true;
    apply_tracker_view_defaults();
    return true;
}

void gps_tracker_open_modal()
{
    if (!is_alive())
    {
        return;
    }
    if (SD.cardType() == CARD_NONE)
    {
        show_toast("No SD Card", 1200);
        return;
    }

    if (!modal_open(g_gps_state.tracker_modal, lv_screen_active(), app_g))
    {
        return;
    }

    modal_set_size(g_gps_state.tracker_modal, 280, 190);
    build_tracker_modal();
}

void gps_tracker_draw_event(lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    if (!e || lv_event_get_code(e) != LV_EVENT_DRAW_POST)
    {
        return;
    }
    if (!g_gps_state.tracker_overlay_active)
    {
        return;
    }

    compute_screen_points();
    const auto& pts = g_gps_state.tracker_screen_points;
    if (pts.empty())
    {
        return;
    }

    lv_layer_t* layer = lv_event_get_layer(e);
    if (!layer)
    {
        return;
    }

    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.bg_color = lv_color_hex(kTrackColor);
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    dot_dsc.border_width = 0;

    const size_t count = pts.size();
    const uint8_t opa_min = 60;
    const uint8_t opa_max = 255;

    for (size_t i = 0; i < count; ++i)
    {
        float t = 1.0f;
        if (count > 1)
        {
            t = 1.0f - static_cast<float>(i) / static_cast<float>(count - 1);
        }
        int opa = static_cast<int>(opa_min + t * (opa_max - opa_min));
        if (opa < opa_min) opa = opa_min;
        if (opa > opa_max) opa = opa_max;
        dot_dsc.bg_opa = static_cast<lv_opa_t>(opa);

        const auto& p = pts[i];
        lv_area_t a;
        a.x1 = p.x - 3;
        a.y1 = p.y - 3;
        a.x2 = p.x + 3;
        a.y2 = p.y + 3;
        lv_draw_rect(layer, &dot_dsc, &a);
    }
}

void gps_tracker_cleanup()
{
    if (!is_alive())
    {
        if (modal_is_open(g_gps_state.tracker_modal))
        {
            modal_close(g_gps_state.tracker_modal);
        }
        g_gps_state.tracker_overlay_active = false;
        g_gps_state.tracker_points.clear();
        g_gps_state.tracker_screen_points.clear();
        g_gps_state.tracker_file.clear();
        return;
    }
    close_tracker_modal();
    g_gps_state.tracker_overlay_active = false;
    g_gps_state.tracker_points.clear();
    g_gps_state.tracker_screen_points.clear();
    g_gps_state.tracker_file.clear();
}
