#include "gps_route_overlay.h"

#include "../../../app/app_context.h"
#include "../../widgets/map/map_tiles.h"
#include "gps_constants.h"
#include "gps_page_components.h"
#include "gps_page_lifetime.h"
#include "gps_page_map.h"
#include "gps_state.h"

#include <SD.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
using gps::ui::lifetime::is_alive;

constexpr double kMinDistanceM = 2.0;
constexpr int kMaxRoutePoints = 240;
constexpr int kMaxDrawPoints = 180;
constexpr int kDefaultRouteZoom = 16;
constexpr uint32_t kRouteColor = 0xEBA341; // GPS panel orange

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

void update_bounds(RouteBounds& b, double lat, double lng)
{
    if (!b.valid)
    {
        b.valid = true;
        b.min_lat = b.max_lat = lat;
        b.min_lng = b.max_lng = lng;
        return;
    }
    b.min_lat = std::min(b.min_lat, lat);
    b.max_lat = std::max(b.max_lat, lat);
    b.min_lng = std::min(b.min_lng, lng);
    b.max_lng = std::max(b.max_lng, lng);
}

void downsample_points(std::vector<GPSPageState::TrackOverlayPoint>& points)
{
    if (points.size() <= (size_t)kMaxRoutePoints)
    {
        return;
    }
    const size_t total = points.size();
    std::vector<GPSPageState::TrackOverlayPoint> reduced;
    reduced.reserve(kMaxRoutePoints);
    for (size_t i = 0; i < (size_t)kMaxRoutePoints; ++i)
    {
        const size_t idx = (i * (total - 1)) / (kMaxRoutePoints - 1);
        reduced.push_back(points[idx]);
    }
    points.swap(reduced);
}

bool parse_lon_lat(const String& token, double& lon, double& lat)
{
    int comma = token.indexOf(',');
    if (comma >= 0)
    {
        int comma2 = token.indexOf(',', comma + 1);
        String lon_str = token.substring(0, comma);
        String lat_str = (comma2 > comma) ? token.substring(comma + 1, comma2)
                                          : token.substring(comma + 1);
        lon = lon_str.toDouble();
        lat = lat_str.toDouble();
        return true;
    }

    int space = token.indexOf(' ');
    if (space < 0)
    {
        return false;
    }
    int space2 = token.indexOf(' ', space + 1);
    String lon_str = token.substring(0, space);
    String lat_str = (space2 > space) ? token.substring(space + 1, space2)
                                      : token.substring(space + 1);
    lon = lon_str.toDouble();
    lat = lat_str.toDouble();
    return true;
}

void append_point(std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                  bool& have_last,
                  GPSPageState::TrackOverlayPoint& last,
                  RouteBounds& bounds,
                  double lat,
                  double lon)
{
    GPSPageState::TrackOverlayPoint pt;
    pt.lat = lat;
    pt.lng = lon;

    if (have_last)
    {
        const double min_d = sampling_distance_m();
        const double d = haversine_m(last.lat, last.lng, pt.lat, pt.lng);
        if (d < min_d)
        {
            return;
        }
    }

    out_points.push_back(pt);
    update_bounds(bounds, lat, lon);
    last = pt;
    have_last = true;
}

void parse_coordinate_tokens(const String& block,
                             std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                             bool& have_last,
                             GPSPageState::TrackOverlayPoint& last,
                             RouteBounds& bounds)
{
    int i = 0;
    const int len = block.length();
    while (i < len)
    {
        while (i < len && block[i] <= ' ')
        {
            ++i;
        }
        if (i >= len)
        {
            break;
        }
        int start = i;
        while (i < len && block[i] > ' ')
        {
            ++i;
        }
        String token = block.substring(start, i);
        double lon = 0.0;
        double lat = 0.0;
        if (parse_lon_lat(token, lon, lat))
        {
            append_point(out_points, have_last, last, bounds, lat, lon);
        }
    }
}

bool load_kml_points(const char* path,
                     std::vector<GPSPageState::TrackOverlayPoint>& out_points,
                     RouteBounds& bounds)
{
    out_points.clear();
    bounds = RouteBounds{};

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

    bool in_gx_track = false;
    bool in_linestring = false;
    bool in_coords_block = false;
    bool has_gx_coords = false;

    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
        {
            continue;
        }

        if (line.indexOf("<gx:Track") >= 0)
        {
            in_gx_track = true;
        }
        if (line.indexOf("</gx:Track>") >= 0)
        {
            in_gx_track = false;
        }
        if (line.indexOf("<LineString") >= 0)
        {
            in_linestring = true;
        }
        if (line.indexOf("</LineString>") >= 0)
        {
            in_linestring = false;
            in_coords_block = false;
        }

        if (in_gx_track && line.indexOf("<gx:coord>") >= 0)
        {
            has_gx_coords = true;
            int start = line.indexOf("<gx:coord>");
            start += 10;
            int end = line.indexOf("</gx:coord>", start);
            if (end < start)
            {
                end = line.length();
            }
            String payload = line.substring(start, end);
            payload.trim();
            double lon = 0.0;
            double lat = 0.0;
            if (parse_lon_lat(payload, lon, lat))
            {
                append_point(out_points, have_last, last, bounds, lat, lon);
            }
            continue;
        }

        if (!has_gx_coords)
        {
            bool has_coords_tag = line.indexOf("<coordinates>") >= 0;
            bool has_coords_end = line.indexOf("</coordinates>") >= 0;
            if ((in_linestring || in_coords_block) && (has_coords_tag || in_coords_block))
            {
                String payload = line;
                if (has_coords_tag)
                {
                    int start = payload.indexOf("<coordinates>");
                    start += 13;
                    payload = payload.substring(start);
                }
                if (has_coords_end)
                {
                    int end = payload.indexOf("</coordinates>");
                    if (end >= 0)
                    {
                        payload = payload.substring(0, end);
                    }
                }
                payload.trim();
                if (payload.length() > 0)
                {
                    parse_coordinate_tokens(payload, out_points, have_last, last, bounds);
                }
                if (has_coords_tag && !has_coords_end)
                {
                    in_coords_block = true;
                }
                else if (has_coords_end)
                {
                    in_coords_block = false;
                }
            }
        }
    }

    f.close();

    if (out_points.empty())
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
    auto& s = g_gps_state;

    if (!s.route_overlay_active || s.route_points.empty())
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

    const auto* anchor = s.tile_ctx.anchor;
    if (!anchor || !anchor->valid)
    {
        return;
    }

    const uint32_t point_count = static_cast<uint32_t>(s.route_points.size());
    const bool cache_valid =
        s.route_cache_zoom == s.zoom_level &&
        s.route_cache_pan_x == s.pan_x &&
        s.route_cache_pan_y == s.pan_y &&
        s.route_cache_map_w == w &&
        s.route_cache_map_h == h &&
        s.route_cache_anchor_valid == anchor->valid &&
        s.route_cache_anchor_px_x == anchor->gps_global_pixel_x &&
        s.route_cache_anchor_px_y == anchor->gps_global_pixel_y &&
        s.route_cache_anchor_screen_x == anchor->gps_tile_screen_x &&
        s.route_cache_anchor_screen_y == anchor->gps_tile_screen_y &&
        s.route_cache_offset_x == anchor->gps_offset_x &&
        s.route_cache_offset_y == anchor->gps_offset_y &&
        s.route_cache_point_count == point_count;

    if (cache_valid && !s.route_screen_points.empty())
    {
        return;
    }

    s.route_screen_points.clear();

    const int total = static_cast<int>(s.route_points.size());
    const int stride = std::max(1, total / kMaxDrawPoints);

    for (int i = 0; i < total && (int)s.route_screen_points.size() < kMaxDrawPoints; i += stride)
    {
        const auto& pt = s.route_points[(size_t)i];
        double map_lat = 0.0;
        double map_lon = 0.0;
        gps_map_transform(pt.lat, pt.lng, map_lat, map_lon);
        int sx = 0;
        int sy = 0;
        if (!gps_screen_pos(s.tile_ctx, map_lat, map_lon, sx, sy))
        {
            continue;
        }

        lv_point_t p;
        p.x = (lv_coord_t)sx;
        p.y = (lv_coord_t)sy;
        s.route_screen_points.push_back(p);
    }

    s.route_cache_zoom = s.zoom_level;
    s.route_cache_pan_x = s.pan_x;
    s.route_cache_pan_y = s.pan_y;
    s.route_cache_map_w = w;
    s.route_cache_map_h = h;
    s.route_cache_anchor_valid = anchor->valid;
    s.route_cache_anchor_px_x = anchor->gps_global_pixel_x;
    s.route_cache_anchor_px_y = anchor->gps_global_pixel_y;
    s.route_cache_anchor_screen_x = anchor->gps_tile_screen_x;
    s.route_cache_anchor_screen_y = anchor->gps_tile_screen_y;
    s.route_cache_offset_x = anchor->gps_offset_x;
    s.route_cache_offset_y = anchor->gps_offset_y;
    s.route_cache_point_count = point_count;
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
    const double MAX_LAT = 85.05112878;
    if (lat > MAX_LAT) lat = MAX_LAT;
    if (lat < -MAX_LAT) lat = -MAX_LAT;
    while (lng < -180.0) lng += 360.0;
    while (lng >= 180.0) lng -= 360.0;

    double n = pow(2.0, zoom);
    double lat_rad = lat * M_PI / 180.0;
    px = ((lng + 180.0) / 360.0 * n) * TILE_SIZE;
    py = ((1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / M_PI) / 2.0 * n) * TILE_SIZE;
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

    for (int z = gps_ui::kMaxZoom; z >= gps_ui::kMinZoom; --z)
    {
        double x1 = 0.0;
        double y1 = 0.0;
        double x2 = 0.0;
        double y2 = 0.0;
        latlng_to_world_px(bounds.min_lat, bounds.min_lng, z, x1, y1);
        latlng_to_world_px(bounds.max_lat, bounds.max_lng, z, x2, y2);
        double dx = std::fabs(x2 - x1);
        double dy = std::fabs(y2 - y1);

        const double world_px = pow(2.0, z) * TILE_SIZE;
        if (dx > world_px / 2.0)
        {
            dx = world_px - dx;
        }

        if (dx <= usable_w && dy <= usable_h)
        {
            out_zoom = z;
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

    app::AppContext& app_ctx = app::AppContext::getInstance();
    const auto& cfg = app_ctx.getConfig();
    if (!cfg.route_enabled || cfg.route_path[0] == '\0')
    {
        clear_route_state();
        return false;
    }

    if (g_gps_state.route_overlay_active && g_gps_state.route_file == cfg.route_path)
    {
        return true;
    }

    std::vector<GPSPageState::TrackOverlayPoint> pts;
    RouteBounds bounds;
    if (!load_kml_points(cfg.route_path, pts, bounds))
    {
        if (show_fail_toast)
        {
            show_toast("Route load failed", 1500);
        }
        clear_route_state();
        return false;
    }

    g_gps_state.route_points = std::move(pts);
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
        int map_w = lv_obj_get_width(g_gps_state.map);
        int map_h = lv_obj_get_height(g_gps_state.map);
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
    const auto& pts = g_gps_state.route_screen_points;
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
    dot_dsc.bg_color = lv_color_hex(kRouteColor);
    dot_dsc.radius = LV_RADIUS_CIRCLE;
    dot_dsc.border_width = 0;

    const size_t count = pts.size();
    const uint8_t opa_min = 50;
    const uint8_t opa_max = 220;

    for (size_t i = 0; i < count; ++i)
    {
        float t = 1.0f;
        if (count > 1)
        {
            t = static_cast<float>(i) / static_cast<float>(count - 1);
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

void gps_route_cleanup()
{
    clear_route_state();
}
