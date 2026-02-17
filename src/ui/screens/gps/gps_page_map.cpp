#include "gps_page_map.h"
#include "../../../app/app_context.h"
#include "../../gps/gps_hw_status.h"
#include "../../ui_common.h"
#include "../../widgets/map/map_tiles.h"
#include "../team/team_state.h"
#include "../team/team_ui_store.h"
#include "gps_constants.h"
#include "gps_page_components.h"
#include "gps_page_lifetime.h"
#include "gps_page_styles.h"
#include "gps_state.h"
#include "gps_tracker_overlay.h"
#include "lvgl.h"
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

// GPS marker icon (room-24px), defined in C image file
extern "C"
{
    extern const lv_image_dsc_t room_24px;
}

#define GPS_DEBUG 0
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

extern GPSPageState g_gps_state;

using gps::ui::lifetime::is_alive;

// GPS data access - now provided by TLoRaPagerBoard
#include "../../gps/gps_service_api.h"

using GPSData = gps::GpsState;

// Cached state for title/status updates (moved from presenter)
static struct
{
    bool cached_has_fix;
    int cached_zoom;
    bool cached_sd_ready;
    bool cached_gps_ready;
    bool cached_has_map_data;
    uint8_t cached_satellites;
    bool initialized;
} last_status_state = {false, 0, false, false, false, 0, false};

static double approx_distance_m(double lat1, double lng1, double lat2, double lng2)
{
    const double kDegToRad = M_PI / 180.0;
    const double kEarthRadiusM = 6371000.0;

    double lat1_rad = lat1 * kDegToRad;
    double lat2_rad = lat2 * kDegToRad;
    double dlat = (lat2 - lat1) * kDegToRad;
    double dlng = (lng2 - lng1) * kDegToRad;

    // Equirectangular approximation (fast, enough for small jitter)
    double x = dlng * cos(0.5 * (lat1_rad + lat2_rad));
    double y = dlat;
    return sqrt(x * x + y * y) * kEarthRadiusM;
}

namespace
{
constexpr double kCoordPi = 3.14159265358979323846;
constexpr double kCoordA = 6378245.0;
constexpr double kCoordEe = 0.00669342162296594323;

bool coord_out_of_china(double lat, double lon)
{
    return (lon < 72.004 || lon > 137.8347 || lat < 0.8293 || lat > 55.8271);
}

double coord_transform_lat(double x, double y)
{
    double ret = -100.0 + 2.0 * x + 3.0 * y + 0.2 * y * y + 0.1 * x * y +
                 0.2 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kCoordPi) + 20.0 * std::sin(2.0 * x * kCoordPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(y * kCoordPi) + 40.0 * std::sin(y / 3.0 * kCoordPi)) * 2.0 / 3.0;
    ret += (160.0 * std::sin(y / 12.0 * kCoordPi) + 320 * std::sin(y * kCoordPi / 30.0)) * 2.0 / 3.0;
    return ret;
}

double coord_transform_lon(double x, double y)
{
    double ret = 300.0 + x + 2.0 * y + 0.1 * x * x + 0.1 * x * y +
                 0.1 * std::sqrt(std::fabs(x));
    ret += (20.0 * std::sin(6.0 * x * kCoordPi) + 20.0 * std::sin(2.0 * x * kCoordPi)) * 2.0 / 3.0;
    ret += (20.0 * std::sin(x * kCoordPi) + 40.0 * std::sin(x / 3.0 * kCoordPi)) * 2.0 / 3.0;
    ret += (150.0 * std::sin(x / 12.0 * kCoordPi) + 300.0 * std::sin(x / 30.0 * kCoordPi)) * 2.0 / 3.0;
    return ret;
}

void wgs84_to_gcj02(double lat, double lon, double& out_lat, double& out_lon)
{
    if (coord_out_of_china(lat, lon))
    {
        out_lat = lat;
        out_lon = lon;
        return;
    }
    double dlat = coord_transform_lat(lon - 105.0, lat - 35.0);
    double dlon = coord_transform_lon(lon - 105.0, lat - 35.0);
    double radlat = lat / 180.0 * kCoordPi;
    double magic = std::sin(radlat);
    magic = 1 - kCoordEe * magic * magic;
    double sqrt_magic = std::sqrt(magic);
    dlat = (dlat * 180.0) / ((kCoordA * (1 - kCoordEe)) / (magic * sqrt_magic) * kCoordPi);
    dlon = (dlon * 180.0) / (kCoordA / sqrt_magic * std::cos(radlat) * kCoordPi);
    out_lat = lat + dlat;
    out_lon = lon + dlon;
}

void gcj02_to_bd09(double lat, double lon, double& out_lat, double& out_lon)
{
    double z = std::sqrt(lon * lon + lat * lat) + 0.00002 * std::sin(lat * kCoordPi);
    double theta = std::atan2(lat, lon) + 0.000003 * std::cos(lon * kCoordPi);
    out_lon = z * std::cos(theta) + 0.0065;
    out_lat = z * std::sin(theta) + 0.006;
}
} // namespace

void gps_map_transform(double lat, double lon, double& out_lat, double& out_lon)
{
    uint8_t coord_system = app::AppContext::getInstance().getConfig().map_coord_system;
    if (coord_system == 1)
    {
        wgs84_to_gcj02(lat, lon, out_lat, out_lon);
        return;
    }
    if (coord_system == 2)
    {
        double gcj_lat = 0.0;
        double gcj_lon = 0.0;
        wgs84_to_gcj02(lat, lon, gcj_lat, gcj_lon);
        gcj02_to_bd09(gcj_lat, gcj_lon, out_lat, out_lon);
        return;
    }
    out_lat = lat;
    out_lon = lon;
}

static inline void sync_map_render_options_from_config()
{
    const auto& cfg = app::AppContext::getInstance().getConfig();
    set_map_render_options(cfg.map_source, cfg.map_contour_enabled);
}

namespace
{
constexpr int kTeamMarkerSize = 10;
constexpr int kTeamMarkerLabelWidth = 44;
constexpr int kTeamMarkerLabelOffsetX = 6;
constexpr int kTeamMarkerLabelOffsetY = 0;
constexpr uint32_t kTeamMarkerColor = 0x00AEEF;
constexpr uint32_t kTeamMarkerBorder = 0xFFFFFF;
constexpr uint32_t kTeamMarkerRefreshMs = 1000;
constexpr uint32_t kMemberPanelRefreshMs = 2000;
constexpr uint32_t kInvalidMemberId = 0xFFFFFFFFu;

uint32_t hash_member_list(const std::vector<team::ui::TeamMemberUi>& members)
{
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v)
    {
        h ^= v;
        h *= 16777619u;
    };
    for (const auto& m : members)
    {
        uint32_t node_id = m.node_id;
        if (node_id == 0)
        {
            node_id = app::AppContext::getInstance().getSelfNodeId();
        }
        mix(node_id);
        mix(static_cast<uint32_t>(team::ui::team_color_index_from_node_id(node_id)));
        for (char c : m.name)
        {
            mix(static_cast<uint8_t>(c));
        }
    }
    return h;
}

void ensure_member_colors(std::vector<team::ui::TeamMemberUi>& members)
{
    for (auto& m : members)
    {
        uint32_t node_id = m.node_id;
        if (node_id == 0)
        {
            node_id = app::AppContext::getInstance().getSelfNodeId();
        }
        m.color_index = team::ui::team_color_index_from_node_id(node_id);
    }
}

bool load_team_data(team::TeamId& out_id, std::vector<team::ui::TeamMemberUi>& out_members)
{
    if (team::ui::g_team_state.in_team && team::ui::g_team_state.has_team_id)
    {
        out_id = team::ui::g_team_state.team_id;
        out_members = team::ui::g_team_state.members;
        ensure_member_colors(out_members);
        return true;
    }

    team::ui::TeamUiSnapshot snap;
    if (team::ui::team_ui_get_store().load(snap) && snap.in_team && snap.has_team_id)
    {
        out_id = snap.team_id;
        out_members = snap.members;
        ensure_member_colors(out_members);
        return true;
    }
    return false;
}

bool member_exists(const std::vector<team::ui::TeamMemberUi>& members, uint32_t member_id)
{
    return std::find_if(members.begin(), members.end(),
                        [&](const team::ui::TeamMemberUi& m)
                        {
                            return m.node_id == member_id;
                        }) != members.end();
}

const team::ui::TeamMemberUi* find_member(const std::vector<team::ui::TeamMemberUi>& members, uint32_t member_id)
{
    auto it = std::find_if(members.begin(), members.end(),
                           [&](const team::ui::TeamMemberUi& m)
                           {
                               return m.node_id == member_id;
                           });
    if (it == members.end())
    {
        return nullptr;
    }
    return &(*it);
}

uint32_t resolve_member_color(const std::vector<team::ui::TeamMemberUi>& members, uint32_t member_id)
{
    auto it = std::find_if(members.begin(), members.end(),
                           [&](const team::ui::TeamMemberUi& m)
                           {
                               return m.node_id == member_id;
                           });
    if (it == members.end())
    {
        return kTeamMarkerColor;
    }
    if (it->color_index >= team::ui::kTeamMaxMembers)
    {
        return kTeamMarkerColor;
    }
    return team::ui::team_color_from_index(it->color_index);
}

lv_color_t marker_text_color(uint32_t color)
{
    uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(color & 0xFF);
    uint32_t lum = (static_cast<uint32_t>(r) * 299 +
                    static_cast<uint32_t>(g) * 587 +
                    static_cast<uint32_t>(b) * 114) /
                   1000;
    return (lum > 160) ? lv_color_black() : lv_color_white();
}

std::string resolve_member_label(const std::vector<team::ui::TeamMemberUi>& members, uint32_t member_id)
{
    std::string contact_name = app::AppContext::getInstance().getContactService().getContactName(member_id);
    if (!contact_name.empty())
    {
        return contact_name;
    }
    const team::ui::TeamMemberUi* member = find_member(members, member_id);
    if (member && !member->name.empty())
    {
        return member->name;
    }
    char fallback[8];
    snprintf(fallback, sizeof(fallback), "%04lX", static_cast<unsigned long>(member_id & 0xFFFFu));
    return std::string(fallback);
}

std::string resolve_member_label(uint32_t member_id, const std::string& member_name)
{
    std::string contact_name = app::AppContext::getInstance().getContactService().getContactName(member_id);
    if (!contact_name.empty())
    {
        return contact_name;
    }
    if (!member_name.empty())
    {
        return member_name;
    }
    char fallback[8];
    snprintf(fallback, sizeof(fallback), "%04lX", static_cast<unsigned long>(member_id & 0xFFFFu));
    return std::string(fallback);
}

lv_obj_t* create_team_marker_obj(uint32_t color)
{
    if (!g_gps_state.map)
    {
        return nullptr;
    }
    lv_obj_t* obj = lv_obj_create(g_gps_state.map);
    lv_obj_set_size(obj, kTeamMarkerSize, kTeamMarkerSize);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(kTeamMarkerBorder), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t* create_team_marker_label(const std::string& text, uint32_t color)
{
    if (!g_gps_state.map)
    {
        return nullptr;
    }
    lv_obj_t* label = lv_label_create(g_gps_state.map);
    lv_label_set_text(label, text.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, kTeamMarkerLabelWidth);
    lv_obj_set_style_bg_opa(label, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_hor(label, 3, 0);
    lv_obj_set_style_pad_ver(label, 1, 0);
    lv_obj_set_style_radius(label, 4, 0);
    lv_obj_set_style_text_color(label, marker_text_color(color), 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

void update_team_marker_label(lv_obj_t* label, const std::string& text, uint32_t color)
{
    if (!label)
    {
        return;
    }
    lv_label_set_text(label, text.c_str());
    lv_obj_set_style_bg_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(label, marker_text_color(color), 0);
}

int find_team_marker_index(uint32_t member_id)
{
    for (size_t i = 0; i < g_gps_state.team_markers.size(); ++i)
    {
        if (g_gps_state.team_markers[i].member_id == member_id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}
} // namespace

void update_resolution_display()
{
    if (!is_alive() || g_gps_state.resolution_label == NULL)
    {
        return;
    }

    double lat = g_gps_state.has_fix ? g_gps_state.lat : gps_ui::kDefaultLat;
    double lon = g_gps_state.has_fix ? g_gps_state.lng : gps_ui::kDefaultLng;
    double map_lat = 0.0;
    double map_lon = 0.0;
    gps_map_transform(lat, lon, map_lat, map_lon);

    double resolution_m = gps::calculate_map_resolution(g_gps_state.zoom_level, map_lat);

    char resolution_text[32];
    if (resolution_m < 1000.0)
    {
        if (resolution_m < 1.0)
        {
            snprintf(resolution_text, sizeof(resolution_text), "%.2f m", resolution_m);
        }
        else
        {
            snprintf(resolution_text, sizeof(resolution_text), "%.0f m", resolution_m);
        }
    }
    else
    {
        double resolution_km = resolution_m / 1000.0;
        if (resolution_km < 10.0)
        {
            snprintf(resolution_text, sizeof(resolution_text), "%.2f km", resolution_km);
        }
        else if (resolution_km < 100.0)
        {
            snprintf(resolution_text, sizeof(resolution_text), "%.1f km", resolution_km);
        }
        else
        {
            snprintf(resolution_text, sizeof(resolution_text), "%.0f km", resolution_km);
        }
    }

    lv_label_set_text(g_gps_state.resolution_label, resolution_text);
}

void update_altitude_display(const GPSData& gps_data)
{
    if (!is_alive() || g_gps_state.altitude_label == NULL)
    {
        return;
    }

    char alt_text[32];
    if (gps_data.valid && gps_data.has_alt)
    {
        snprintf(alt_text, sizeof(alt_text), "Alt: %.0f m", gps_data.alt_m);
    }
    else
    {
        snprintf(alt_text, sizeof(alt_text), "Alt: -- m");
    }
    lv_label_set_text(g_gps_state.altitude_label, alt_text);
}

void update_title_and_status()
{
    if (!is_alive())
    {
        return;
    }
    bool sd_ready = sd_hw_is_ready();
    bool gps_ready = gps_hw_is_ready();
    GPSData gps_data = gps::gps_get_data();
    uint8_t satellites = gps_data.satellites;

    bool state_changed = !last_status_state.initialized ||
                         (last_status_state.cached_has_fix != g_gps_state.has_fix ||
                          last_status_state.cached_sd_ready != sd_ready ||
                          last_status_state.cached_gps_ready != gps_ready ||
                          last_status_state.cached_has_map_data != g_gps_state.has_visible_map_data ||
                          last_status_state.cached_satellites != satellites);

    if (!state_changed)
    {
        GPS_LOG("[GPS] State unchanged, skipping title update (will be handled by 30s timer)\n");
        return;
    }

    GPS_LOG("[GPS] State changed, updating title: has_fix=%d, gps_ready=%d, sd_ready=%d, has_map=%d\n",
            g_gps_state.has_fix, gps_ready, sd_ready, g_gps_state.has_visible_map_data);

    last_status_state.cached_has_fix = g_gps_state.has_fix;
    last_status_state.cached_zoom = g_gps_state.zoom_level;
    last_status_state.cached_sd_ready = sd_ready;
    last_status_state.cached_gps_ready = gps_ready;
    last_status_state.cached_has_map_data = g_gps_state.has_visible_map_data;
    last_status_state.cached_satellites = satellites;
    last_status_state.initialized = true;

    // Update shared top bar title; layout no longer depends on lv_menu
    static char title_buffer[64];

    if (g_gps_state.has_fix && gps_ready)
    {
        char coord_buf[64];
        uint8_t coord_fmt = app::AppContext::getInstance().getConfig().gps_coord_format;
        ui_format_coords(g_gps_state.lat, g_gps_state.lng, coord_fmt, coord_buf, sizeof(coord_buf));
        snprintf(title_buffer, sizeof(title_buffer), "Map - %s", coord_buf);
    }
    else if (!sd_ready)
    {
        snprintf(title_buffer, sizeof(title_buffer), "Map - No SD Card");
    }
    else if (!g_gps_state.has_visible_map_data)
    {
        uint8_t source = sanitize_map_source(app::AppContext::getInstance().getConfig().map_source);
        if (map_source_directory_available(source))
        {
            snprintf(title_buffer, sizeof(title_buffer), "Map - No Map Data");
        }
        else
        {
            snprintf(title_buffer, sizeof(title_buffer), "Map - %s Missing", map_source_label(source));
        }
    }
    else
    {
        snprintf(title_buffer, sizeof(title_buffer), "Map - no gps data");
    }

    GPS_LOG("[GPS] Setting page title to: '%s' (page=%p)\n",
            title_buffer, g_gps_state.page);

    if (g_gps_state.top_bar.container != nullptr)
    {
        ::ui::widgets::top_bar_set_title(g_gps_state.top_bar, title_buffer);
        // Also update shared top bar battery from board state
        ui_update_top_bar_battery(g_gps_state.top_bar);
    }

    update_zoom_btn();
}

void update_zoom_btn()
{
    // Currently empty - placeholder for future zoom button updates
}

void reset_title_status_cache()
{
    last_status_state.initialized = false;
}

void update_map_anchor()
{
    if (!is_alive())
    {
        return;
    }
    sync_map_render_options_from_config();
    double map_lat = 0.0;
    double map_lon = 0.0;
    gps_map_transform(g_gps_state.lat, g_gps_state.lng, map_lat, map_lon);
    ::update_map_anchor(g_gps_state.tile_ctx,
                        map_lat, map_lon,
                        g_gps_state.zoom_level,
                        g_gps_state.pan_x, g_gps_state.pan_y,
                        g_gps_state.has_fix);
}

void update_map_tiles(bool lightweight)
{
    if (!is_alive() || g_gps_state.map == NULL)
    {
        return;
    }
    sync_map_render_options_from_config();

    double map_lat = 0.0;
    double map_lon = 0.0;
    gps_map_transform(g_gps_state.lat, g_gps_state.lng, map_lat, map_lon);
    ::calculate_required_tiles(g_gps_state.tile_ctx,
                               map_lat, map_lon,
                               g_gps_state.zoom_level,
                               g_gps_state.pan_x, g_gps_state.pan_y,
                               g_gps_state.has_fix);

    if (!lightweight)
    {
        fix_ui_elements_position();
    }

    if (!lightweight)
    {
        bool zoom_changed = (g_gps_state.last_resolution_zoom != g_gps_state.zoom_level);
        bool lat_changed = (fabs(g_gps_state.last_resolution_lat - map_lat) > 0.001);
        if (zoom_changed || lat_changed)
        {
            update_resolution_display();
            g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
            g_gps_state.last_resolution_lat = map_lat;
        }

        // Update GPS marker position after map tiles are updated
        // This ensures marker is rendered on top and moves with the map
        if (g_gps_state.gps_marker != NULL)
        {
            update_gps_marker_position();
        }
        update_team_marker_positions();
    }

    lv_obj_invalidate(g_gps_state.map);
}

/**
 * Update GPS marker position based on current GPS coordinates and map anchor
 * Called after map tiles are laid out to ensure marker is rendered on top
 */
void update_gps_marker_position()
{
    if (!is_alive() || g_gps_state.gps_marker == NULL || g_gps_state.map == NULL)
    {
        return;
    }

    if (!g_gps_state.has_fix || !g_gps_state.tile_ctx.anchor || !g_gps_state.tile_ctx.anchor->valid)
    {
        lv_obj_add_flag(g_gps_state.gps_marker, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Calculate screen position for GPS coordinates
    int screen_x, screen_y;
    double map_lat = 0.0;
    double map_lon = 0.0;
    gps_map_transform(g_gps_state.lat, g_gps_state.lng, map_lat, map_lon);
    if (gps_screen_pos(g_gps_state.tile_ctx, map_lat, map_lon, screen_x, screen_y))
    {
        // Center marker on GPS position (marker is typically 24x24, so offset by half)
        const int marker_size = 24;
        lv_obj_set_pos(g_gps_state.gps_marker, screen_x - marker_size / 2, screen_y - marker_size / 2);
        lv_obj_clear_flag(g_gps_state.gps_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_gps_state.gps_marker);
    }
    else
    {
        lv_obj_add_flag(g_gps_state.gps_marker, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * Create GPS marker if GPS data is available
 * Called when position button is clicked
 */
void create_gps_marker()
{
    if (!is_alive() || !g_gps_state.has_fix || g_gps_state.map == NULL)
    {
        return;
    }

    // If marker already exists, just update its position
    if (g_gps_state.gps_marker != NULL)
    {
        update_gps_marker_position();
        return;
    }

    // Create marker image with room icon
    g_gps_state.gps_marker = lv_image_create(g_gps_state.map);
    lv_image_set_src(g_gps_state.gps_marker, &room_24px);

    // Set marker size (24x24 pixels)
    lv_obj_set_size(g_gps_state.gps_marker, 24, 24);

    // Set initial position
    update_gps_marker_position();

    GPS_LOG("[GPS] GPS marker created at lat=%.6f, lng=%.6f\n", g_gps_state.lat, g_gps_state.lng);
}

/**
 * Hide GPS marker
 */
void hide_gps_marker()
{
    if (!is_alive())
    {
        return;
    }
    if (g_gps_state.gps_marker != NULL)
    {
        lv_obj_add_flag(g_gps_state.gps_marker, LV_OBJ_FLAG_HIDDEN);
    }
}

void clear_team_markers()
{
    for (auto& marker : g_gps_state.team_markers)
    {
        if (marker.obj)
        {
            lv_obj_del(marker.obj);
            marker.obj = nullptr;
        }
        if (marker.label)
        {
            lv_obj_del(marker.label);
            marker.label = nullptr;
        }
    }
    g_gps_state.team_markers.clear();
}

static void clear_member_panel_buttons()
{
    for (auto* btn : g_gps_state.member_btns)
    {
        if (!btn)
        {
            continue;
        }
        if (g_gps_state.app_group)
        {
            lv_group_remove_obj(btn);
        }
        lv_obj_del(btn);
    }
    g_gps_state.member_btns.clear();
    g_gps_state.member_btn_ids.clear();
}

static void update_member_button_states()
{
    for (size_t i = 0; i < g_gps_state.member_btns.size(); ++i)
    {
        lv_obj_t* btn = g_gps_state.member_btns[i];
        if (!btn)
        {
            continue;
        }
        bool selected = (g_gps_state.member_btn_ids[i] == g_gps_state.selected_member_id);
        lv_obj_set_style_outline_width(btn, selected ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_outline_color(btn, lv_color_hex(kTeamMarkerBorder), LV_PART_MAIN);
    }
}

static void select_member(uint32_t member_id)
{
    if (member_id == 0)
    {
        return;
    }
    g_gps_state.selected_member_id = member_id;
    g_gps_state.team_marker_last_ms = 0;
    clear_team_markers();
    update_member_button_states();
}

static void member_btn_event_cb(lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_KEY)
    {
        return;
    }
    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = static_cast<lv_key_t>(lv_event_get_key(e));
        if (key != LV_KEY_ENTER)
        {
            return;
        }
    }
    extern void updateUserActivity();
    updateUserActivity();
    uint32_t member_id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    if (member_id == 0 || member_id == kInvalidMemberId)
    {
        return;
    }
    select_member(member_id);
    refresh_team_markers_from_posring();

    team::TeamId team_id{};
    std::vector<team::ui::TeamMemberUi> members;
    if (!load_team_data(team_id, members))
    {
        return;
    }
    std::string track_path;
    if (team::ui::team_ui_get_member_track_path(team_id, member_id, track_path))
    {
        gps_tracker_load_file(track_path.c_str(), true);
    }
}

static lv_obj_t* create_member_button(const team::ui::TeamMemberUi& member, uint32_t color)
{
    if (!g_gps_state.member_panel)
    {
        return nullptr;
    }
    lv_obj_t* btn = lv_btn_create(g_gps_state.member_panel);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, 28);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    gps::ui::styles::apply_control_button(btn);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

    lv_obj_t* dot = lv_obj_create(btn);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dot, lv_color_hex(kTeamMarkerBorder), LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 3, 0);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    std::string label_text = resolve_member_label(member.node_id, member.name);
    lv_label_set_text(label, label_text.c_str());
    gps::ui::styles::apply_control_button_label(label);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_pad_left(label, 16, 0);
    lv_obj_set_style_pad_right(label, 6, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    return btn;
}

void refresh_member_panel(bool force)
{
    if (!is_alive() || !g_gps_state.member_panel)
    {
        return;
    }
    uint32_t now_ms = millis();
    if (!force && (now_ms - g_gps_state.member_panel_last_ms) < kMemberPanelRefreshMs)
    {
        return;
    }
    g_gps_state.member_panel_last_ms = now_ms;

    team::TeamId team_id{};
    std::vector<team::ui::TeamMemberUi> members;
    bool route_visible = (g_gps_state.route_btn != nullptr) &&
                         !lv_obj_has_flag(g_gps_state.route_btn, LV_OBJ_FLAG_HIDDEN);
    if (!load_team_data(team_id, members) || members.empty())
    {
        if (g_gps_state.member_panel)
        {
            if (route_visible)
            {
                lv_obj_clear_flag(g_gps_state.member_panel, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(g_gps_state.member_panel, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (!g_gps_state.member_btns.empty() || g_gps_state.member_list_hash != 0)
        {
            clear_member_panel_buttons();
            g_gps_state.member_list_hash = 0;
            g_gps_state.selected_member_id = kInvalidMemberId;
            clear_team_markers();
        }
        return;
    }
    (void)team_id;
    lv_obj_clear_flag(g_gps_state.member_panel, LV_OBJ_FLAG_HIDDEN);

    uint32_t hash = hash_member_list(members);
    bool rebuild = force ||
                   hash != g_gps_state.member_list_hash ||
                   members.size() != g_gps_state.member_btns.size();

    if (rebuild)
    {
        clear_member_panel_buttons();
        g_gps_state.member_btn_ids.reserve(members.size());
        g_gps_state.member_btns.reserve(members.size());
        for (const auto& m : members)
        {
            uint32_t color = resolve_member_color(members, m.node_id);
            lv_obj_t* btn = create_member_button(m, color);
            if (!btn)
            {
                continue;
            }
            void* user_data = reinterpret_cast<void*>(static_cast<uintptr_t>(m.node_id));
            lv_obj_add_event_cb(btn, member_btn_event_cb, LV_EVENT_CLICKED, user_data);
            lv_obj_add_event_cb(btn, member_btn_event_cb, LV_EVENT_KEY, user_data);
            if (g_gps_state.app_group)
            {
                lv_group_add_obj(g_gps_state.app_group, btn);
            }
            g_gps_state.member_btns.push_back(btn);
            g_gps_state.member_btn_ids.push_back(m.node_id);
        }
        g_gps_state.member_list_hash = hash;
        fix_ui_elements_position();
    }

    if (!member_exists(members, g_gps_state.selected_member_id))
    {
        g_gps_state.selected_member_id = kInvalidMemberId;
        clear_team_markers();
    }

    update_member_button_states();
}

void update_team_marker_positions()
{
    if (!is_alive() || g_gps_state.map == NULL)
    {
        return;
    }
    if (!g_gps_state.tile_ctx.anchor || !g_gps_state.tile_ctx.anchor->valid)
    {
        for (auto& marker : g_gps_state.team_markers)
        {
            if (marker.obj)
            {
                lv_obj_add_flag(marker.obj, LV_OBJ_FLAG_HIDDEN);
            }
            if (marker.label)
            {
                lv_obj_add_flag(marker.label, LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }
    for (auto& marker : g_gps_state.team_markers)
    {
        if (!marker.obj)
        {
            continue;
        }
        double lat = static_cast<double>(marker.lat_e7) / 1e7;
        double lng = static_cast<double>(marker.lon_e7) / 1e7;
        double map_lat = 0.0;
        double map_lon = 0.0;
        gps_map_transform(lat, lng, map_lat, map_lon);
        int screen_x = 0;
        int screen_y = 0;
        if (gps_screen_pos(g_gps_state.tile_ctx, map_lat, map_lon, screen_x, screen_y))
        {
            lv_obj_set_pos(marker.obj,
                           screen_x - kTeamMarkerSize / 2,
                           screen_y - kTeamMarkerSize / 2);
            lv_obj_clear_flag(marker.obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(marker.obj);
            if (marker.label)
            {
                lv_obj_update_layout(marker.label);
                int label_h = lv_obj_get_height(marker.label);
                int label_x = screen_x + kTeamMarkerSize / 2 + kTeamMarkerLabelOffsetX;
                int label_y = screen_y - (label_h / 2) + kTeamMarkerLabelOffsetY;
                lv_obj_set_pos(marker.label, label_x, label_y);
                lv_obj_clear_flag(marker.label, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(marker.label);
            }
        }
        else
        {
            lv_obj_add_flag(marker.obj, LV_OBJ_FLAG_HIDDEN);
            if (marker.label)
            {
                lv_obj_add_flag(marker.label, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void refresh_team_markers_from_posring()
{
    if (!is_alive() || g_gps_state.map == NULL)
    {
        return;
    }
    if (g_gps_state.selected_member_id == kInvalidMemberId)
    {
        clear_team_markers();
        return;
    }
    uint32_t now_ms = millis();
    if (now_ms - g_gps_state.team_marker_last_ms < kTeamMarkerRefreshMs)
    {
        return;
    }
    g_gps_state.team_marker_last_ms = now_ms;

    team::TeamId team_id{};
    std::vector<team::ui::TeamMemberUi> members;
    if (!load_team_data(team_id, members))
    {
        clear_team_markers();
        return;
    }
    if (!member_exists(members, g_gps_state.selected_member_id))
    {
        clear_team_markers();
        return;
    }
    std::string label_text = resolve_member_label(members, g_gps_state.selected_member_id);

    std::vector<team::ui::TeamPosSample> samples;
    if (!team::ui::team_ui_posring_load_latest(team_id, samples))
    {
        clear_team_markers();
        return;
    }
    auto sample_it = std::find_if(samples.begin(), samples.end(),
                                  [&](const team::ui::TeamPosSample& s)
                                  {
                                      return s.member_id == g_gps_state.selected_member_id;
                                  });
    if (sample_it == samples.end())
    {
        clear_team_markers();
        return;
    }

    for (auto it = g_gps_state.team_markers.begin(); it != g_gps_state.team_markers.end();)
    {
        if (it->member_id != g_gps_state.selected_member_id)
        {
            if (it->obj)
            {
                lv_obj_del(it->obj);
            }
            if (it->label)
            {
                lv_obj_del(it->label);
            }
            it = g_gps_state.team_markers.erase(it);
            continue;
        }
        ++it;
    }

    uint32_t color = resolve_member_color(members, g_gps_state.selected_member_id);
    int idx = find_team_marker_index(g_gps_state.selected_member_id);
    if (idx < 0)
    {
        GPSPageState::TeamMarker marker;
        marker.member_id = sample_it->member_id;
        marker.lat_e7 = sample_it->lat_e7;
        marker.lon_e7 = sample_it->lon_e7;
        marker.ts = sample_it->ts;
        marker.color = color;
        marker.obj = create_team_marker_obj(color);
        marker.label = create_team_marker_label(label_text, color);
        g_gps_state.team_markers.push_back(marker);
    }
    else
    {
        auto& marker = g_gps_state.team_markers[idx];
        marker.lat_e7 = sample_it->lat_e7;
        marker.lon_e7 = sample_it->lon_e7;
        marker.ts = sample_it->ts;
        if (!marker.obj)
        {
            marker.obj = create_team_marker_obj(color);
        }
        if (!marker.label)
        {
            marker.label = create_team_marker_label(label_text, color);
        }
        update_team_marker_label(marker.label, label_text, color);
        if (marker.color != color && marker.obj)
        {
            lv_obj_set_style_bg_color(marker.obj, lv_color_hex(color), LV_PART_MAIN);
            marker.color = color;
        }
    }

    update_team_marker_positions();
}

void tick_loader()
{
    if (!is_alive())
    {
        return;
    }
    sync_map_render_options_from_config();
    static bool prev_has_visible_map_data = false;

    if (!g_gps_state.initial_tiles_loaded && g_gps_state.map != NULL)
    {
        g_gps_state.initial_tiles_loaded = true;
        update_map_tiles(false);
        g_gps_state.initial_load_ms = millis();
    }

    ::tile_loader_step(g_gps_state.tile_ctx);

    uint8_t missing_source = 0;
    if (take_missing_tile_notice(&missing_source))
    {
        if (sd_hw_is_ready() && map_source_directory_available(missing_source))
        {
            show_toast("No tile in this area", 1500);
        }
    }
}

void tick_gps_update(bool allow_map_refresh)
{
    if (!is_alive())
    {
        return;
    }
    GPSData gps_data = gps::gps_get_data();
    update_altitude_display(gps_data);

    static bool prev_has_fix = false;
    static bool prev_has_visible_map_data = false;
    static bool prev_gps_ready = false;
    static bool prev_sd_ready = false;
    static uint8_t prev_satellites = 0;
    static uint32_t last_title_update_ms = 0;

    // 新增：地图刷新节流状态
    static double last_refresh_lat = 0.0;
    static double last_refresh_lng = 0.0;
    static bool last_refresh_valid = false;
    static uint32_t last_refresh_ms = 0;

    const uint32_t TITLE_UPDATE_INTERVAL_MS = 30000;
    const double MOVE_THRESHOLD_M = 15.0;      // 忽略小抖动（可调 10~30m）
    const uint32_t REFRESH_INTERVAL_MS = 2000; // 即使移动很慢也定期刷新

    bool gps_ready = gps_hw_is_ready();
    bool sd_ready = sd_hw_is_ready();
    uint32_t now_ms = millis();

    bool gps_state_changed = false;
    if (gps_data.valid)
    {
        double new_lat = gps_data.lat;
        double new_lng = gps_data.lng;

        bool just_got_fix = !g_gps_state.has_fix;

        // 始终更新坐标，让状态显示保持最新
        if (just_got_fix ||
            fabs(new_lat - g_gps_state.lat) > 0.0001 ||
            fabs(new_lng - g_gps_state.lng) > 0.0001)
        {
            g_gps_state.lat = new_lat;
            g_gps_state.lng = new_lng;
            g_gps_state.has_fix = true;
            gps_state_changed = true;
        }

        if (just_got_fix && g_gps_state.zoom_level == 0)
        {
            g_gps_state.zoom_level = gps_ui::kDefaultZoom;
            g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
            double map_lat = 0.0;
            double map_lon = 0.0;
            gps_map_transform(g_gps_state.lat, g_gps_state.lng, map_lat, map_lon);
            g_gps_state.last_resolution_lat = map_lat;
            update_resolution_display();
        }

        // 只对“地图刷新”加抖动过滤
        if (allow_map_refresh)
        {
            // Manual pan mode disables GPS auto-follow until user explicitly re-centers with [P]osition.
            if (g_gps_state.follow_position)
            {
                bool moved_enough = false;
                if (!last_refresh_valid)
                {
                    moved_enough = true;
                }
                else
                {
                    double dist_m = approx_distance_m(
                        last_refresh_lat, last_refresh_lng, new_lat, new_lng);
                    moved_enough = dist_m >= MOVE_THRESHOLD_M;
                }

                bool time_due = (now_ms - last_refresh_ms) >= REFRESH_INTERVAL_MS;

                if (just_got_fix || moved_enough || time_due)
                {
                    g_gps_state.pan_x = 0;
                    g_gps_state.pan_y = 0;

                    double map_lat = 0.0;
                    double map_lon = 0.0;
                    gps_map_transform(g_gps_state.lat, g_gps_state.lng, map_lat, map_lon);
                    g_gps_state.last_resolution_lat = map_lat;
                    update_map_tiles(false);

                    last_refresh_lat = new_lat;
                    last_refresh_lng = new_lng;
                    last_refresh_ms = now_ms;
                    last_refresh_valid = true;
                }
            }
        }
    }
    else
    {
        if (g_gps_state.has_fix)
        {
            g_gps_state.has_fix = false;
            g_gps_state.zoom_level = 0;

            g_gps_state.lat = gps_ui::kDefaultLat;
            g_gps_state.lng = gps_ui::kDefaultLng;

            g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
            double map_lat = 0.0;
            double map_lon = 0.0;
            gps_map_transform(g_gps_state.lat, g_gps_state.lng, map_lat, map_lon);
            g_gps_state.last_resolution_lat = map_lat;
            update_resolution_display();
            gps_state_changed = true;

            last_refresh_valid = false;

            if (allow_map_refresh)
            {
                g_gps_state.pan_x = 0;
                g_gps_state.pan_y = 0;
                update_map_tiles(false);
            }
        }
    }

    bool state_changed = (prev_has_fix != g_gps_state.has_fix ||
                          prev_has_visible_map_data != g_gps_state.has_visible_map_data ||
                          prev_gps_ready != gps_ready ||
                          prev_sd_ready != sd_ready ||
                          prev_satellites != gps_data.satellites ||
                          gps_state_changed);
    bool time_elapsed = (now_ms - last_title_update_ms) >= TITLE_UPDATE_INTERVAL_MS;

    if (state_changed || time_elapsed)
    {
        GPS_LOG("[GPS] tick_gps_update: Updating title (state_changed=%d, time_elapsed=%d, has_fix=%d, has_map=%d)\n",
                state_changed, time_elapsed, g_gps_state.has_fix, g_gps_state.has_visible_map_data);
        reset_title_status_cache();
        update_title_and_status();
        last_title_update_ms = now_ms;

        prev_has_fix = g_gps_state.has_fix;
        prev_has_visible_map_data = g_gps_state.has_visible_map_data;
        prev_gps_ready = gps_ready;
        prev_sd_ready = sd_ready;
        prev_satellites = gps_data.satellites;
    }

    // Update GPS marker position if marker exists and GPS data changed
    if (gps_state_changed && g_gps_state.gps_marker != NULL)
    {
        update_gps_marker_position();
    }
}
