#include "ui/screens/gps/gps_tracker_overlay.h"

#include "platform/ui/device_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "ui/localization.h"
#include "ui/screens/gps/gps_modal.h"
#include "ui/screens/gps/gps_page_lifetime.h"
#include "ui/screens/gps/gps_page_map.h"
#include "ui/screens/gps/gps_page_styles.h"
#include "ui/screens/gps/gps_state.h"
#include "ui/support/lvgl_fs_utils.h"
#include "ui/ui_theme.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_tiles.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern void show_toast(const char* message, uint32_t duration_ms);

namespace
{
using gps::ui::lifetime::is_alive;

constexpr double kMinDistanceM = 2.0;
constexpr int kMaxDrawPoints = 150;
constexpr int kDefaultTrackerZoom = 16;
std::vector<std::string> s_modal_names;

double deg2rad(double deg)
{
    return deg * 0.017453292519943295;
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

std::string trim_copy(std::string value)
{
    const auto is_space = [](unsigned char ch)
    {
        return std::isspace(ch) != 0;
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch)
                                            { return !is_space(static_cast<unsigned char>(ch)); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch)
                             { return !is_space(static_cast<unsigned char>(ch)); })
                    .base(),
                value.end());
    return value;
}

bool ends_with_ignore_case(const std::string& value, const char* suffix)
{
    const std::size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len)
    {
        return false;
    }
    const std::size_t start = value.size() - suffix_len;
    for (std::size_t index = 0; index < suffix_len; ++index)
    {
        const unsigned char lhs = static_cast<unsigned char>(value[start + index]);
        const unsigned char rhs = static_cast<unsigned char>(suffix[index]);
        if (std::tolower(lhs) != std::tolower(rhs))
        {
            return false;
        }
    }
    return true;
}

bool parse_double_token(const std::string& token, double& out)
{
    char* end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end != token.c_str();
}

bool parse_attr_double(const std::string& line, const char* key, double& out)
{
    const std::string token = std::string(key) + "=\"";
    const std::size_t start = line.find(token);
    if (start == std::string::npos)
    {
        return false;
    }
    const std::size_t value_start = start + token.size();
    const std::size_t value_end = line.find('"', value_start);
    if (value_end == std::string::npos || value_end <= value_start)
    {
        return false;
    }
    return parse_double_token(line.substr(value_start, value_end - value_start), out);
}

void downsample_points(std::vector<GPSPageState::TrackOverlayPoint>& points)
{
    if (points.size() <= static_cast<std::size_t>(kMaxDrawPoints))
    {
        return;
    }
    const std::size_t total = points.size();
    std::vector<GPSPageState::TrackOverlayPoint> reduced;
    reduced.reserve(kMaxDrawPoints);
    for (std::size_t index = 0; index < static_cast<std::size_t>(kMaxDrawPoints); ++index)
    {
        const std::size_t src = (index * (total - 1)) / (static_cast<std::size_t>(kMaxDrawPoints) - 1);
        reduced.push_back(points[src]);
    }
    points.swap(reduced);
}

void append_point(std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                  bool& have_last,
                  GPSPageState::TrackOverlayPoint& last,
                  double lat,
                  double lon)
{
    GPSPageState::TrackOverlayPoint point{};
    point.lat = lat;
    point.lng = lon;

    if (have_last)
    {
        const double distance = haversine_m(last.lat, last.lng, point.lat, point.lng);
        if (distance < sampling_distance_m())
        {
            return;
        }
    }

    out_points.push_back(point);
    downsample_points(out_points);
    last = point;
    have_last = true;
}

bool load_gpx_points(const char* path, std::vector<GPSPageState::TrackOverlayPoint>& out_points)
{
    out_points.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    std::string text;
    if (!ui::fs::read_text_file(path, text))
    {
        return false;
    }

    bool have_last = false;
    GPSPageState::TrackOverlayPoint last{};
    std::size_t line_start = 0;
    while (line_start <= text.size())
    {
        const std::size_t line_end = text.find('\n', line_start);
        std::string line = line_end == std::string::npos ? text.substr(line_start)
                                                         : text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.find("<trkpt") != std::string::npos)
        {
            double lat = 0.0;
            double lon = 0.0;
            if (parse_attr_double(line, "lat", lat) && parse_attr_double(line, "lon", lon))
            {
                append_point(out_points, have_last, last, lat, lon);
            }
        }

        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 1;
    }

    return !out_points.empty();
}

bool load_csv_points(const char* path, std::vector<GPSPageState::TrackOverlayPoint>& out_points)
{
    out_points.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    std::string text;
    if (!ui::fs::read_text_file(path, text))
    {
        return false;
    }

    bool have_last = false;
    GPSPageState::TrackOverlayPoint last{};
    std::size_t line_start = 0;
    while (line_start <= text.size())
    {
        const std::size_t line_end = text.find('\n', line_start);
        std::string line = line_end == std::string::npos ? text.substr(line_start)
                                                         : text.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        line = trim_copy(std::move(line));
        if (!line.empty())
        {
            const char ch = line.front();
            if ((ch >= '0' && ch <= '9') || ch == '-')
            {
                const std::size_t comma1 = line.find(',');
                if (comma1 != std::string::npos && comma1 > 0)
                {
                    const std::size_t comma2 = line.find(',', comma1 + 1);
                    const std::string lat_str = line.substr(0, comma1);
                    const std::string lon_str = comma2 != std::string::npos ? line.substr(comma1 + 1, comma2 - comma1 - 1)
                                                                            : line.substr(comma1 + 1);

                    double lat = 0.0;
                    double lon = 0.0;
                    if (parse_double_token(lat_str, lat) && parse_double_token(lon_str, lon))
                    {
                        append_point(out_points, have_last, last, lat, lon);
                    }
                }
            }
        }

        if (line_end == std::string::npos)
        {
            break;
        }
        line_start = line_end + 1;
    }

    return !out_points.empty();
}

void compute_screen_points()
{
    if (!is_alive())
    {
        return;
    }
    auto& state = g_gps_state;
    state.tracker_screen_points.clear();

    if (!state.tracker_overlay_active || state.tracker_points.empty())
    {
        return;
    }
    if (!state.tile_ctx.anchor || !state.tile_ctx.anchor->valid || !state.map)
    {
        return;
    }

    const int width = lv_obj_get_width(state.map);
    const int height = lv_obj_get_height(state.map);
    if (width <= 0 || height <= 0)
    {
        return;
    }

    const int total = static_cast<int>(state.tracker_points.size());
    const int stride = std::max(1, total / kMaxDrawPoints);

    bool have_prev = false;
    GPSPageState::TrackOverlayPoint prev{};
    for (int index = total - 1; index >= 0 && static_cast<int>(state.tracker_screen_points.size()) < kMaxDrawPoints;
         index -= stride)
    {
        const auto& point = state.tracker_points[static_cast<std::size_t>(index)];
        if (have_prev)
        {
            const double distance = haversine_m(prev.lat, prev.lng, point.lat, point.lng);
            if (distance < sampling_distance_m())
            {
                continue;
            }
        }

        double map_lat = 0.0;
        double map_lon = 0.0;
        gps_map_transform(point.lat, point.lng, map_lat, map_lon);
        int sx = 0;
        int sy = 0;
        if (!gps_screen_pos(state.tile_ctx, map_lat, map_lon, sx, sy))
        {
            break;
        }

        lv_point_t screen_point{};
        screen_point.x = static_cast<lv_coord_t>(sx);
        screen_point.y = static_cast<lv_coord_t>(sy);
        state.tracker_screen_points.push_back(screen_point);

        prev = point;
        have_prev = true;
    }
}

void apply_tracker_view_defaults()
{
    if (!is_alive())
    {
        return;
    }
    auto& state = g_gps_state;
    if (state.tracker_points.empty())
    {
        return;
    }

    const auto& last = state.tracker_points.back();
    state.zoom_level = kDefaultTrackerZoom;
    state.pan_x = 0;
    state.pan_y = 0;
    state.follow_position = false;
    state.lat = last.lat;
    state.lng = last.lng;

    reset_title_status_cache();
    update_title_and_status();
    update_resolution_display();
    update_map_tiles(false);

    if (state.map)
    {
        lv_obj_invalidate(state.map);
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
    bind_navigation_inputs_to_group(app_g);
}

void on_tracker_close_clicked(lv_event_t* /*e*/)
{
    close_tracker_modal();
}

void on_tracker_bg_clicked(lv_event_t* e)
{
    if (!e || lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    lv_obj_t* target = lv_event_get_target_obj(e);
    if (target != g_gps_state.tracker_modal.bg)
    {
        return;
    }

    close_tracker_modal();
}

void on_track_selected(lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }

    const std::uintptr_t index = reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(e));
    if (index >= s_modal_names.size())
    {
        return;
    }

    const std::string path = std::string(platform::ui::tracker::track_dir()) + "/" + s_modal_names[index];
    if (!gps_tracker_load_file(path.c_str(), true))
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
    auto& state = g_gps_state;
    const bool touch_layout = modal_uses_touch_layout();

    lv_obj_t* list_parent = state.tracker_modal.win;
    if (touch_layout)
    {
        gps::ui::styles::apply_zoom_popup_win(state.tracker_modal.win);
        lv_obj_t* title_bar = modal_create_touch_title_bar(state.tracker_modal.win, "Select Track");
        const lv_coord_t title_height = title_bar != nullptr ? lv_obj_get_height(title_bar) : modal_resolve_title_height();
        list_parent = modal_create_touch_content_area(state.tracker_modal.win, title_height);
        if (list_parent == nullptr)
        {
            return;
        }
        lv_obj_set_style_pad_row(list_parent, 10, 0);
    }
    else
    {
        lv_obj_t* title = lv_label_create(state.tracker_modal.win);
        ::ui::i18n::set_label_text(title, "Select Track");
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    }

    lv_obj_t* list = lv_list_create(list_parent);
    if (touch_layout)
    {
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_height(list, 0);
        lv_obj_set_flex_grow(list, 1);
        lv_obj_set_style_pad_left(list, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_right(list, 0, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_size(list, LV_PCT(100), 90);
        lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 28);
    }
    gps::ui::styles::apply_tracker_modal_list(list);

    lv_obj_t* close_btn = nullptr;
    if (touch_layout)
    {
        close_btn = modal_create_touch_action_button(list_parent, "Close", on_tracker_close_clicked, nullptr, LV_PCT(100));
    }
    else
    {
        close_btn = lv_btn_create(state.tracker_modal.win);
        lv_obj_set_size(close_btn, 100, 24);
        lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -4);
        gps::ui::styles::apply_control_button(close_btn);
        lv_obj_t* close_label = lv_label_create(close_btn);
        ::ui::i18n::set_label_text(close_label, "Close");
        gps::ui::styles::apply_control_button_label(close_label);
        lv_obj_center(close_label);
        lv_obj_add_event_cb(close_btn, on_tracker_close_clicked, LV_EVENT_CLICKED, nullptr);
    }

    s_modal_names.clear();
    platform::ui::tracker::list_tracks(s_modal_names, 64);
    std::sort(s_modal_names.begin(), s_modal_names.end());

    lv_group_remove_all_objs(state.tracker_modal.group);
    if (s_modal_names.empty())
    {
        lv_obj_t* label = lv_label_create(list);
        ::ui::i18n::set_label_text(label, "No track files");
        gps::ui::styles::apply_control_button_label(label);
        if (touch_layout)
        {
            lv_obj_set_width(label, LV_PCT(100));
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        }
        lv_group_add_obj(state.tracker_modal.group, close_btn);
        set_default_group(state.tracker_modal.group);
        bind_navigation_inputs_to_group(state.tracker_modal.group);
        lv_group_focus_obj(close_btn);
        return;
    }

    const lv_coord_t track_row_height = touch_layout ? 60 : 26;
    const lv_coord_t track_row_pad_ver = touch_layout ? 10 : 2;
    const lv_coord_t track_row_pad_hor = touch_layout ? 12 : 6;
    lv_obj_t* first_btn = nullptr;

    for (std::size_t index = 0; index < s_modal_names.size(); ++index)
    {
        const std::string& name = s_modal_names[index];
        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_FILE, name.c_str());
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_height(btn, track_row_height);
        lv_obj_set_style_pad_ver(btn, track_row_pad_ver, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(btn, track_row_pad_hor, LV_PART_MAIN);
        gps::ui::styles::apply_control_button(btn);
        const uint32_t child_count = lv_obj_get_child_cnt(btn);
        for (uint32_t child_index = 0; child_index < child_count; ++child_index)
        {
            lv_obj_t* child = lv_obj_get_child(btn, child_index);
            if (child && lv_obj_check_type(child, &lv_label_class))
            {
                gps::ui::styles::apply_control_button_label(child);
                if (touch_layout && child_index == child_count - 1)
                {
                    lv_label_set_long_mode(child, LV_LABEL_LONG_SCROLL_CIRCULAR);
                }
            }
        }
        lv_obj_add_event_cb(btn,
                            on_track_selected,
                            LV_EVENT_CLICKED,
                            reinterpret_cast<void*>(index));
        lv_group_add_obj(state.tracker_modal.group, btn);
        if (first_btn == nullptr)
        {
            first_btn = btn;
        }
    }
    lv_group_add_obj(state.tracker_modal.group, close_btn);

    set_default_group(state.tracker_modal.group);
    bind_navigation_inputs_to_group(state.tracker_modal.group);
    if (first_btn != nullptr)
    {
        lv_group_focus_obj(first_btn);
    }
}

} // namespace

bool gps_tracker_load_file(const char* path, bool show_fail_toast)
{
    if (!is_alive() || path == nullptr || path[0] == '\0')
    {
        return false;
    }

    std::vector<GPSPageState::TrackOverlayPoint> points;
    const std::string lower_path = ui::fs::normalize_path(path);
    const bool loaded = ends_with_ignore_case(lower_path, ".csv") ? load_csv_points(path, points)
                                                                  : load_gpx_points(path, points);
    if (!loaded)
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
    g_gps_state.tracker_points = std::move(points);
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
    if (!platform::ui::device::sd_ready())
    {
        show_toast("No SD Card", 1200);
        return;
    }
    if (!modal_open(g_gps_state.tracker_modal, lv_screen_active(), app_g))
    {
        return;
    }

    if (g_gps_state.tracker_modal.bg != nullptr)
    {
        lv_obj_add_event_cb(g_gps_state.tracker_modal.bg, on_tracker_bg_clicked, LV_EVENT_CLICKED, nullptr);
    }

    const bool touch_layout = modal_uses_touch_layout();
    modal_set_requested_size(g_gps_state.tracker_modal, touch_layout ? 520 : 280, touch_layout ? 620 : 190);
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
    const auto& points = g_gps_state.tracker_screen_points;
    if (points.empty())
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
    dot_dsc.bg_color = ::ui::theme::map_track();
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    dot_dsc.border_width = 0;

    const std::size_t count = points.size();
    const uint8_t opa_min = 60;
    const uint8_t opa_max = 255;
    for (std::size_t index = 0; index < count; ++index)
    {
        float t = 1.0f;
        if (count > 1)
        {
            t = 1.0f - static_cast<float>(index) / static_cast<float>(count - 1);
        }
        int opa = static_cast<int>(opa_min + t * (opa_max - opa_min));
        if (opa < opa_min) opa = opa_min;
        if (opa > opa_max) opa = opa_max;
        dot_dsc.bg_opa = static_cast<lv_opa_t>(opa);

        const auto& point = points[index];
        lv_area_t area{};
        area.x1 = point.x - 3;
        area.y1 = point.y - 3;
        area.x2 = point.x + 3;
        area.y2 = point.y + 3;
        lv_draw_rect(layer, &dot_dsc, &area);
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
