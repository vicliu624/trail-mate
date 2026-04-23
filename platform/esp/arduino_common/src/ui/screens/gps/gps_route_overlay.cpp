#include "ui/screens/gps/gps_route_overlay.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/device_runtime.h"
#include "ui/screens/gps/gps_constants.h"
#include "ui/screens/gps/gps_page_components.h"
#include "ui/screens/gps/gps_page_lifetime.h"
#include "ui/screens/gps/gps_page_map.h"
#include "ui/screens/gps/gps_state.h"
#include "ui/support/lvgl_fs_utils.h"
#include "ui/ui_theme.h"
#include "ui/widgets/map/map_tiles.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using gps::ui::lifetime::is_alive;

constexpr double kMinDistanceM = 2.0;
constexpr int kMaxRoutePoints = 240;
constexpr int kMaxDrawPoints = 180;
constexpr int kDefaultRouteZoom = 16;
struct RouteBounds
{
    bool valid = false;
    double min_lat = 0.0;
    double min_lng = 0.0;
    double max_lat = 0.0;
    double max_lng = 0.0;
};

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

void update_bounds(RouteBounds& bounds, double lat, double lng)
{
    if (!bounds.valid)
    {
        bounds.valid = true;
        bounds.min_lat = bounds.max_lat = lat;
        bounds.min_lng = bounds.max_lng = lng;
        return;
    }

    bounds.min_lat = std::min(bounds.min_lat, lat);
    bounds.max_lat = std::max(bounds.max_lat, lat);
    bounds.min_lng = std::min(bounds.min_lng, lng);
    bounds.max_lng = std::max(bounds.max_lng, lng);
}

void downsample_points(std::vector<GPSPageState::TrackOverlayPoint>& points)
{
    if (points.size() <= static_cast<std::size_t>(kMaxRoutePoints))
    {
        return;
    }

    const std::size_t total = points.size();
    std::vector<GPSPageState::TrackOverlayPoint> reduced;
    reduced.reserve(kMaxRoutePoints);
    for (std::size_t index = 0; index < static_cast<std::size_t>(kMaxRoutePoints); ++index)
    {
        const std::size_t src = (index * (total - 1)) / (static_cast<std::size_t>(kMaxRoutePoints) - 1);
        reduced.push_back(points[src]);
    }
    points.swap(reduced);
}

void append_point(std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                  bool& have_last,
                  GPSPageState::TrackOverlayPoint& last,
                  RouteBounds& bounds,
                  double lat,
                  double lon);

bool parse_double_token(const std::string& token, double& out)
{
    char* end = nullptr;
    out = std::strtod(token.c_str(), &end);
    return end != token.c_str();
}

bool parse_lon_lat_token(const std::string& token, double& lon, double& lat)
{
    std::size_t split = token.find(',');
    if (split == std::string::npos)
    {
        split = token.find_first_of(" \t\r\n");
    }
    if (split == std::string::npos || split == 0)
    {
        return false;
    }

    const std::size_t lat_start = token.find_first_not_of(", \t\r\n", split);
    if (lat_start == std::string::npos)
    {
        return false;
    }

    const std::size_t lat_end = token.find_first_of(", \t\r\n", lat_start);
    const std::string lon_token = token.substr(0, split);
    const std::string lat_token = lat_end == std::string::npos ? token.substr(lat_start)
                                                               : token.substr(lat_start, lat_end - lat_start);
    return parse_double_token(lon_token, lon) && parse_double_token(lat_token, lat);
}

void parse_coordinate_block(const std::string& block,
                            std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                            bool& have_last,
                            GPSPageState::TrackOverlayPoint& last,
                            RouteBounds& bounds)
{
    const char* cursor = block.c_str();
    while (*cursor != '\0')
    {
        while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)) != 0)
        {
            ++cursor;
        }
        if (*cursor == '\0')
        {
            break;
        }

        const char* token_start = cursor;
        while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)) == 0)
        {
            ++cursor;
        }

        std::string token(token_start, static_cast<std::size_t>(cursor - token_start));
        double lon = 0.0;
        double lat = 0.0;
        if (parse_lon_lat_token(token, lon, lat))
        {
            append_point(out_points, have_last, last, bounds, lat, lon);
        }
    }
}

void append_point(std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                  bool& have_last,
                  GPSPageState::TrackOverlayPoint& last,
                  RouteBounds& bounds,
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
    update_bounds(bounds, lat, lon);
    last = point;
    have_last = true;
}

bool parse_gx_track_points(const std::string& text,
                           std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                           RouteBounds& bounds)
{
    bool have_last = false;
    GPSPageState::TrackOverlayPoint last{};
    bool found = false;

    std::size_t pos = 0;
    while ((pos = text.find("<gx:coord>", pos)) != std::string::npos)
    {
        const std::size_t start = pos + std::strlen("<gx:coord>");
        const std::size_t end = text.find("</gx:coord>", start);
        if (end == std::string::npos)
        {
            break;
        }
        double lon = 0.0;
        double lat = 0.0;
        if (parse_lon_lat_token(text.substr(start, end - start), lon, lat))
        {
            append_point(out_points, have_last, last, bounds, lat, lon);
            found = true;
        }
        pos = end + std::strlen("</gx:coord>");
    }

    return found;
}

bool parse_linestring_points(const std::string& text,
                             std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                             RouteBounds& bounds)
{
    bool have_last = false;
    GPSPageState::TrackOverlayPoint last{};
    bool found = false;

    std::size_t pos = 0;
    while ((pos = text.find("<LineString", pos)) != std::string::npos)
    {
        const std::size_t block_start = text.find('>', pos);
        if (block_start == std::string::npos)
        {
            break;
        }
        const std::size_t block_end = text.find("</LineString>", block_start + 1);
        if (block_end == std::string::npos)
        {
            break;
        }

        const std::string block = text.substr(block_start + 1, block_end - block_start - 1);
        std::size_t coord_pos = 0;
        while ((coord_pos = block.find("<coordinates>", coord_pos)) != std::string::npos)
        {
            const std::size_t coord_start = coord_pos + std::strlen("<coordinates>");
            const std::size_t coord_end = block.find("</coordinates>", coord_start);
            if (coord_end == std::string::npos)
            {
                break;
            }
            parse_coordinate_block(block.substr(coord_start, coord_end - coord_start),
                                   out_points,
                                   have_last,
                                   last,
                                   bounds);
            found = true;
            coord_pos = coord_end + std::strlen("</coordinates>");
        }

        pos = block_end + std::strlen("</LineString>");
    }

    return found;
}

bool parse_any_coordinate_blocks(const std::string& text,
                                 std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                                 RouteBounds& bounds)
{
    bool have_last = false;
    GPSPageState::TrackOverlayPoint last{};
    bool found = false;

    std::size_t pos = 0;
    while ((pos = text.find("<coordinates>", pos)) != std::string::npos)
    {
        const std::size_t start = pos + std::strlen("<coordinates>");
        const std::size_t end = text.find("</coordinates>", start);
        if (end == std::string::npos)
        {
            break;
        }
        parse_coordinate_block(text.substr(start, end - start), out_points, have_last, last, bounds);
        found = true;
        pos = end + std::strlen("</coordinates>");
    }

    return found;
}

bool load_kml_points(const char* path,
                     std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                     RouteBounds& bounds)
{
    out_points.clear();
    bounds = RouteBounds{};
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    std::string text;
    if (!ui::fs::read_text_file(path, text))
    {
        return false;
    }

    bool found = parse_gx_track_points(text, out_points, bounds);
    if (!found)
    {
        found = parse_linestring_points(text, out_points, bounds);
    }
    if (!found)
    {
        found = parse_any_coordinate_blocks(text, out_points, bounds);
    }
    if (!found || out_points.empty())
    {
        return false;
    }

    downsample_points(out_points);
    return true;
}

void compute_screen_points()
{
    if (!is_alive())
    {
        return;
    }
    auto& state = g_gps_state;

    if (!state.route_overlay_active || state.route_points.empty())
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

    const auto* anchor = state.tile_ctx.anchor;
    if (!anchor || !anchor->valid)
    {
        return;
    }

    const uint32_t point_count = static_cast<uint32_t>(state.route_points.size());
    const bool cache_valid =
        state.route_cache_zoom == state.zoom_level &&
        state.route_cache_pan_x == state.pan_x &&
        state.route_cache_pan_y == state.pan_y &&
        state.route_cache_map_w == width &&
        state.route_cache_map_h == height &&
        state.route_cache_anchor_valid == anchor->valid &&
        state.route_cache_anchor_px_x == anchor->gps_global_pixel_x &&
        state.route_cache_anchor_px_y == anchor->gps_global_pixel_y &&
        state.route_cache_anchor_screen_x == anchor->gps_tile_screen_x &&
        state.route_cache_anchor_screen_y == anchor->gps_tile_screen_y &&
        state.route_cache_offset_x == anchor->gps_offset_x &&
        state.route_cache_offset_y == anchor->gps_offset_y &&
        state.route_cache_point_count == point_count;

    if (cache_valid && !state.route_screen_points.empty())
    {
        return;
    }

    state.route_screen_points.clear();
    const int total = static_cast<int>(state.route_points.size());
    const int stride = std::max(1, total / kMaxDrawPoints);
    for (int index = 0; index < total && static_cast<int>(state.route_screen_points.size()) < kMaxDrawPoints;
         index += stride)
    {
        const auto& point = state.route_points[static_cast<std::size_t>(index)];
        double map_lat = 0.0;
        double map_lon = 0.0;
        gps_map_transform(point.lat, point.lng, map_lat, map_lon);
        int sx = 0;
        int sy = 0;
        if (!gps_screen_pos(state.tile_ctx, map_lat, map_lon, sx, sy))
        {
            continue;
        }

        lv_point_t screen_point{};
        screen_point.x = static_cast<lv_coord_t>(sx);
        screen_point.y = static_cast<lv_coord_t>(sy);
        state.route_screen_points.push_back(screen_point);
    }

    state.route_cache_zoom = state.zoom_level;
    state.route_cache_pan_x = state.pan_x;
    state.route_cache_pan_y = state.pan_y;
    state.route_cache_map_w = width;
    state.route_cache_map_h = height;
    state.route_cache_anchor_valid = anchor->valid;
    state.route_cache_anchor_px_x = anchor->gps_global_pixel_x;
    state.route_cache_anchor_px_y = anchor->gps_global_pixel_y;
    state.route_cache_anchor_screen_x = anchor->gps_tile_screen_x;
    state.route_cache_anchor_screen_y = anchor->gps_tile_screen_y;
    state.route_cache_offset_x = anchor->gps_offset_x;
    state.route_cache_offset_y = anchor->gps_offset_y;
    state.route_cache_point_count = point_count;
}

void clear_route_state()
{
    g_gps_state.route_overlay_active = false;
    g_gps_state.route_bbox_valid = false;
    g_gps_state.route_points.clear();
    g_gps_state.route_screen_points.clear();
    g_gps_state.route_file.clear();
    g_gps_state.route_min_lat = 0.0;
    g_gps_state.route_min_lng = 0.0;
    g_gps_state.route_max_lat = 0.0;
    g_gps_state.route_max_lng = 0.0;
    g_gps_state.route_cache_zoom = -1;
    g_gps_state.route_cache_pan_x = 0;
    g_gps_state.route_cache_pan_y = 0;
    g_gps_state.route_cache_anchor_valid = false;
    g_gps_state.route_cache_anchor_px_x = 0;
    g_gps_state.route_cache_anchor_px_y = 0;
    g_gps_state.route_cache_anchor_screen_x = 0;
    g_gps_state.route_cache_anchor_screen_y = 0;
    g_gps_state.route_cache_offset_x = 0;
    g_gps_state.route_cache_offset_y = 0;
    g_gps_state.route_cache_map_w = 0;
    g_gps_state.route_cache_map_h = 0;
    g_gps_state.route_cache_point_count = 0;
}

void latlng_to_world_px(double lat, double lng, int zoom, double& px, double& py)
{
    constexpr double kMaxLat = 85.05112878;
    if (lat > kMaxLat) lat = kMaxLat;
    if (lat < -kMaxLat) lat = -kMaxLat;
    while (lng < -180.0) lng += 360.0;
    while (lng >= 180.0) lng -= 360.0;

    const double n = std::pow(2.0, zoom);
    const double lat_rad = lat * M_PI / 180.0;
    px = ((lng + 180.0) / 360.0 * n) * TILE_SIZE;
    py = ((1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / M_PI) / 2.0 * n) * TILE_SIZE;
}

bool compute_fit_zoom(const RouteBounds& bounds, int map_w, int map_h, int& out_zoom)
{
    if (!bounds.valid || map_w <= 0 || map_h <= 0)
    {
        return false;
    }

    const int margin = 24;
    const int usable_w = std::max(10, map_w - margin);
    const int usable_h = std::max(10, map_h - margin);

    for (int zoom = gps_ui::kMaxZoom; zoom >= gps_ui::kMinZoom; --zoom)
    {
        double x1 = 0.0;
        double y1 = 0.0;
        double x2 = 0.0;
        double y2 = 0.0;
        latlng_to_world_px(bounds.min_lat, bounds.min_lng, zoom, x1, y1);
        latlng_to_world_px(bounds.max_lat, bounds.max_lng, zoom, x2, y2);
        double dx = std::fabs(x2 - x1);
        double dy = std::fabs(y2 - y1);

        const double world_px = std::pow(2.0, zoom) * TILE_SIZE;
        if (dx > world_px / 2.0)
        {
            dx = world_px - dx;
        }

        if (dx <= usable_w && dy <= usable_h)
        {
            out_zoom = zoom;
            return true;
        }
    }

    out_zoom = gps_ui::kMinZoom;
    return true;
}

} // namespace

bool gps_route_sync_from_config(bool show_fail_toast)
{
    if (!is_alive())
    {
        return false;
    }

    const auto& cfg = app::configFacade().getConfig();
    if (!cfg.route_enabled || cfg.route_path[0] == '\0')
    {
        clear_route_state();
        return false;
    }

    if (g_gps_state.route_overlay_active && g_gps_state.route_file == cfg.route_path)
    {
        return true;
    }

    std::vector<GPSPageState::TrackOverlayPoint> points;
    RouteBounds bounds;
    if (!load_kml_points(cfg.route_path, points, bounds))
    {
        if (show_fail_toast)
        {
            show_toast("Route load failed", 1500);
        }
        clear_route_state();
        return false;
    }

    g_gps_state.route_points = std::move(points);
    g_gps_state.route_file = cfg.route_path;
    g_gps_state.route_overlay_active = true;
    g_gps_state.route_bbox_valid = bounds.valid;
    g_gps_state.route_min_lat = bounds.min_lat;
    g_gps_state.route_min_lng = bounds.min_lng;
    g_gps_state.route_max_lat = bounds.max_lat;
    g_gps_state.route_max_lng = bounds.max_lng;
    g_gps_state.route_cache_zoom = -1;
    g_gps_state.route_cache_point_count = 0;
    g_gps_state.route_cache_anchor_valid = false;
    return true;
}

bool gps_route_focus(bool show_fail_toast)
{
    if (!is_alive())
    {
        return false;
    }

    if (!g_gps_state.route_overlay_active || g_gps_state.route_points.empty())
    {
        if (!gps_route_sync_from_config(show_fail_toast))
        {
            return false;
        }
    }

    if (!g_gps_state.route_bbox_valid)
    {
        if (show_fail_toast)
        {
            show_toast("Route not ready", 1500);
        }
        return false;
    }

    const double center_lat = (g_gps_state.route_min_lat + g_gps_state.route_max_lat) * 0.5;
    const double center_lng = (g_gps_state.route_min_lng + g_gps_state.route_max_lng) * 0.5;

    int fit_zoom = kDefaultRouteZoom;
    if (g_gps_state.map)
    {
        const int map_w = lv_obj_get_width(g_gps_state.map);
        const int map_h = lv_obj_get_height(g_gps_state.map);
        RouteBounds bounds;
        bounds.valid = true;
        bounds.min_lat = g_gps_state.route_min_lat;
        bounds.min_lng = g_gps_state.route_min_lng;
        bounds.max_lat = g_gps_state.route_max_lat;
        bounds.max_lng = g_gps_state.route_max_lng;
        compute_fit_zoom(bounds, map_w, map_h, fit_zoom);
    }

    g_gps_state.zoom_level = fit_zoom;
    g_gps_state.pan_x = 0;
    g_gps_state.pan_y = 0;
    g_gps_state.follow_position = false;
    g_gps_state.lat = center_lat;
    g_gps_state.lng = center_lng;

    reset_title_status_cache();
    update_title_and_status();
    update_resolution_display();
    update_map_tiles(false);

    if (g_gps_state.map)
    {
        lv_obj_invalidate(g_gps_state.map);
    }

    return true;
}

void gps_route_draw_event(lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    if (!e || lv_event_get_code(e) != LV_EVENT_DRAW_POST)
    {
        return;
    }
    if (!g_gps_state.route_overlay_active)
    {
        return;
    }

    compute_screen_points();
    const auto& points = g_gps_state.route_screen_points;
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
    dot_dsc.bg_color = ::ui::theme::map_route();
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    dot_dsc.border_width = 0;

    const std::size_t count = points.size();
    const uint8_t opa_min = 50;
    const uint8_t opa_max = 220;
    for (std::size_t index = 0; index < count; ++index)
    {
        float t = 1.0f;
        if (count > 1)
        {
            t = static_cast<float>(index) / static_cast<float>(count - 1);
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

void gps_route_cleanup()
{
    clear_route_state();
}
