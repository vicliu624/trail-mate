#include "gps_page_map.h"
#include "gps_page_lifetime.h"
#include "gps_state.h"
#include "gps_page_components.h"
#include "../../widgets/map/map_tiles.h"
#include "gps_constants.h"
#include "../../gps/gps_hw_status.h"
#include "../../ui_common.h"
#include "lvgl.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>

// GPS marker icon (room-24px), defined in C image file
extern "C" {
    extern const lv_image_dsc_t room_24px;
}

#define GPS_DEBUG 1
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
static struct {
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

void update_resolution_display()
{
    if (!is_alive() || g_gps_state.resolution_label == NULL) {
        return;
    }
    
    double lat = g_gps_state.has_fix ? g_gps_state.lat : gps_ui::kDefaultLat;
    
    double resolution_m = gps::calculate_map_resolution(g_gps_state.zoom_level, lat);
    
    char resolution_text[32];
    if (resolution_m < 1000.0) {
        if (resolution_m < 1.0) {
            snprintf(resolution_text, sizeof(resolution_text), "%.2f m", resolution_m);
        } else {
            snprintf(resolution_text, sizeof(resolution_text), "%.0f m", resolution_m);
        }
    } else {
        double resolution_km = resolution_m / 1000.0;
        if (resolution_km < 10.0) {
            snprintf(resolution_text, sizeof(resolution_text), "%.2f km", resolution_km);
        } else if (resolution_km < 100.0) {
            snprintf(resolution_text, sizeof(resolution_text), "%.1f km", resolution_km);
        } else {
            snprintf(resolution_text, sizeof(resolution_text), "%.0f km", resolution_km);
        }
    }
    
    lv_label_set_text(g_gps_state.resolution_label, resolution_text);
}

void update_title_and_status()
{
    if (!is_alive()) {
        return;
    }
    bool sd_ready = sd_hw_is_ready();
    bool gps_ready = gps_hw_is_ready();
    GPSData gps_data = gps::gps_get_data();
    uint8_t satellites = gps_data.satellites;
    
    if (g_gps_state.status != NULL) {
        char status_text[128];
        
        if (!sd_ready) {
            snprintf(status_text, sizeof(status_text), 
                "SD Card Not Found\nPlease insert SD card\nwith map tiles");
        } else if (!g_gps_state.has_visible_map_data) {
            snprintf(status_text, sizeof(status_text),
                "No Map Data\nPlease add map tiles to\n/sd/maps/ directory");
        } else if (!gps_ready) {
            snprintf(status_text, sizeof(status_text), 
                "GPS Not Ready\nWorld Map\nZoom: %d", g_gps_state.zoom_level);
        } else if (g_gps_state.has_fix) {
            snprintf(status_text, sizeof(status_text), 
                "Lat: %.6f\nLng: %.6f\nZoom: %d\nSat: %d", 
                g_gps_state.lat, g_gps_state.lng, g_gps_state.zoom_level, satellites);
        } else {
            snprintf(status_text, sizeof(status_text), 
                "Searching GPS...\nWorld Map\nZoom: %d", g_gps_state.zoom_level);
        }
        
        lv_label_set_text(g_gps_state.status, status_text);
    }
    
    bool state_changed = !last_status_state.initialized ||
                         (last_status_state.cached_has_fix != g_gps_state.has_fix ||
                         last_status_state.cached_sd_ready != sd_ready ||
                         last_status_state.cached_gps_ready != gps_ready ||
                         last_status_state.cached_has_map_data != g_gps_state.has_visible_map_data ||
                         last_status_state.cached_satellites != satellites);
    
    if (!state_changed) {
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
    
    if (g_gps_state.has_fix && gps_ready) {
        snprintf(title_buffer, sizeof(title_buffer), "GPS - %.4f,%.4f", g_gps_state.lat, g_gps_state.lng);
    } else if (!sd_ready) {
        snprintf(title_buffer, sizeof(title_buffer), "GPS - No SD Card");
    } else if (!g_gps_state.has_visible_map_data) {
        snprintf(title_buffer, sizeof(title_buffer), "GPS - No Map Data");
    } else {
        snprintf(title_buffer, sizeof(title_buffer), "GPS - no gps data");
    }
    
    GPS_LOG("[GPS] Setting page title to: '%s' (page=%p)\n", 
            title_buffer, g_gps_state.page);
    
    if (g_gps_state.top_bar.container != nullptr) {
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
    if (!is_alive()) {
        return;
    }
    ::update_map_anchor(g_gps_state.tile_ctx, 
                       g_gps_state.lat, g_gps_state.lng, 
                       g_gps_state.zoom_level, 
                       g_gps_state.pan_x, g_gps_state.pan_y, 
                       g_gps_state.has_fix);
}

void update_map_tiles(bool lightweight)
{
    if (!is_alive() || g_gps_state.map == NULL) {
        return;
    }

    ::calculate_required_tiles(g_gps_state.tile_ctx, 
                               g_gps_state.lat, g_gps_state.lng, 
                               g_gps_state.zoom_level, 
                               g_gps_state.pan_x, g_gps_state.pan_y, 
                               g_gps_state.has_fix);
    
    if (!lightweight) {
        fix_ui_elements_position();
    }
    
    if (!lightweight) {
        bool zoom_changed = (g_gps_state.last_resolution_zoom != g_gps_state.zoom_level);
        bool lat_changed = (fabs(g_gps_state.last_resolution_lat - g_gps_state.lat) > 0.001);
        if (zoom_changed || lat_changed) {
            update_resolution_display();
            g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
            g_gps_state.last_resolution_lat = g_gps_state.lat;
        }
        
        // Update GPS marker position after map tiles are updated
        // This ensures marker is rendered on top and moves with the map
        if (g_gps_state.gps_marker != NULL) {
            update_gps_marker_position();
        }
    }
    
    lv_obj_invalidate(g_gps_state.map);
}

/**
 * Update GPS marker position based on current GPS coordinates and map anchor
 * Called after map tiles are laid out to ensure marker is rendered on top
 */
void update_gps_marker_position()
{
    if (!is_alive() || g_gps_state.gps_marker == NULL || g_gps_state.map == NULL) {
        return;
    }
    
    if (!g_gps_state.has_fix || !g_gps_state.tile_ctx.anchor || !g_gps_state.tile_ctx.anchor->valid) {
        lv_obj_add_flag(g_gps_state.gps_marker, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    
    // Calculate screen position for GPS coordinates
    int screen_x, screen_y;
    if (gps_screen_pos(g_gps_state.tile_ctx, g_gps_state.lat, g_gps_state.lng, screen_x, screen_y)) {
        // Center marker on GPS position (marker is typically 24x24, so offset by half)
        const int marker_size = 24;
        lv_obj_set_pos(g_gps_state.gps_marker, screen_x - marker_size / 2, screen_y - marker_size / 2);
        lv_obj_clear_flag(g_gps_state.gps_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_gps_state.gps_marker);
    } else {
        lv_obj_add_flag(g_gps_state.gps_marker, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * Create GPS marker if GPS data is available
 * Called when position button is clicked
 */
void create_gps_marker()
{
    if (!is_alive() || !g_gps_state.has_fix || g_gps_state.map == NULL) {
        return;
    }
    
    // If marker already exists, just update its position
    if (g_gps_state.gps_marker != NULL) {
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
    if (!is_alive()) {
        return;
    }
    if (g_gps_state.gps_marker != NULL) {
        lv_obj_add_flag(g_gps_state.gps_marker, LV_OBJ_FLAG_HIDDEN);
    }
}

void tick_loader()
{
    if (!is_alive()) {
        return;
    }
    static bool prev_has_visible_map_data = false;
    
    if (!g_gps_state.initial_tiles_loaded && g_gps_state.map != NULL) {
        g_gps_state.initial_tiles_loaded = true;
        update_map_tiles(false);
        g_gps_state.initial_load_ms = millis();
    }
    
    ::tile_loader_step(g_gps_state.tile_ctx);
    
    static uint32_t last_fix_ui_ms = 0;
    uint32_t now_ms = millis();
    if (now_ms - last_fix_ui_ms > 100) {
        fix_ui_elements_position();
        last_fix_ui_ms = now_ms;
    }
    
}

void tick_gps_update(bool allow_map_refresh)
{
    if (!is_alive()) {
        return;
    }
    GPSData gps_data = gps::gps_get_data();

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
    const double MOVE_THRESHOLD_M = 15.0;        // 忽略小抖动（可调 10~30m）
    const uint32_t REFRESH_INTERVAL_MS = 2000;   // 即使移动很慢也定期刷新

    bool gps_ready = gps_hw_is_ready();
    bool sd_ready = sd_hw_is_ready();
    uint32_t now_ms = millis();

    bool gps_state_changed = false;
    if (gps_data.valid) {
        double new_lat = gps_data.lat;
        double new_lng = gps_data.lng;

        bool just_got_fix = !g_gps_state.has_fix;

        // 始终更新坐标，让状态显示保持最新
        if (just_got_fix ||
            fabs(new_lat - g_gps_state.lat) > 0.0001 ||
            fabs(new_lng - g_gps_state.lng) > 0.0001) {
            g_gps_state.lat = new_lat;
            g_gps_state.lng = new_lng;
            g_gps_state.has_fix = true;
            gps_state_changed = true;
        }

        if (just_got_fix && g_gps_state.zoom_level == 0) {
            g_gps_state.zoom_level = gps_ui::kDefaultZoom;
            g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
            g_gps_state.last_resolution_lat = g_gps_state.lat;
            update_resolution_display();
        }

        // 只对“地图刷新”加抖动过滤
        if (allow_map_refresh) {
            bool moved_enough = false;
            if (!last_refresh_valid) {
                moved_enough = true;
            } else {
                double dist_m = approx_distance_m(
                    last_refresh_lat, last_refresh_lng, new_lat, new_lng);
                moved_enough = dist_m >= MOVE_THRESHOLD_M;
            }

            bool time_due = (now_ms - last_refresh_ms) >= REFRESH_INTERVAL_MS;

            if (just_got_fix || moved_enough || time_due) {
                g_gps_state.pan_x = 0;
                g_gps_state.pan_y = 0;

                g_gps_state.last_resolution_lat = g_gps_state.lat;
                update_map_tiles(false);

                last_refresh_lat = new_lat;
                last_refresh_lng = new_lng;
                last_refresh_ms = now_ms;
                last_refresh_valid = true;
            }
        }
    } else {
        if (g_gps_state.has_fix) {
            g_gps_state.has_fix = false;
            g_gps_state.zoom_level = 0;

            g_gps_state.lat = gps_ui::kDefaultLat;
            g_gps_state.lng = gps_ui::kDefaultLng;

            g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
            g_gps_state.last_resolution_lat = g_gps_state.lat;
            update_resolution_display();
            gps_state_changed = true;

            last_refresh_valid = false;

            if (allow_map_refresh) {
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
    
    if (state_changed || time_elapsed) {
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
    if (gps_state_changed && g_gps_state.gps_marker != NULL) {
        update_gps_marker_position();
    }
}
