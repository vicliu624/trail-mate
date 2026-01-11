#include "gps_page_map.h"
#include "gps_state.h"
#include "gps_page_components.h"
#include "gps_page_presenter.h"
#include "map_tiles.h"
#include "board/TLoRaPagerBoard.h"
#include "gps/GPS.h"
#include "lvgl.h"
#include <Arduino.h>
#include <cmath>

extern void reset_title_status_cache();

#define MAP_PAN_STEP 32
#define DEFAULT_ZOOM 12
#define DEFAULT_LAT 51.5074
#define DEFAULT_LNG -0.1278

#define GPS_DEBUG 0
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

extern GPSPageState g_gps_state;

struct GPSData {
    double lat;
    double lng;
    uint8_t satellites;
    bool valid;
    uint32_t age;
};
extern GPSData getGPSData();

void update_map_anchor()
{
    ::update_map_anchor(g_gps_state.tile_ctx, 
                       g_gps_state.lat, g_gps_state.lng, 
                       g_gps_state.zoom_level, 
                       g_gps_state.pan_x, g_gps_state.pan_y, 
                       g_gps_state.has_fix);
}

void update_map_tiles(bool lightweight)
{
    if (g_gps_state.map == NULL) {
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
    
    bool has_tiles_to_load = false;
    for (const auto &tile : g_gps_state.tiles) {
        if (tile.visible && tile.img_obj == NULL) {
            has_tiles_to_load = true;
            break;
        }
    }
    
    if (has_tiles_to_load && !g_gps_state.loading) {
        g_gps_state.loading = true;
        show_loading();
    } else if (!has_tiles_to_load && g_gps_state.loading) {
        g_gps_state.loading = false;
        hide_loading();
    }
    
    if (!lightweight) {
        bool zoom_changed = (g_gps_state.last_resolution_zoom != g_gps_state.zoom_level);
        bool lat_changed = (fabs(g_gps_state.last_resolution_lat - g_gps_state.lat) > 0.001);
        if (zoom_changed || lat_changed) {
            update_resolution_display();
            g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
            g_gps_state.last_resolution_lat = g_gps_state.lat;
        }
    }
    
    lv_obj_invalidate(g_gps_state.map);
}

void tick_loader()
{
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
    
    bool has_unloaded = false;
    for (const auto &tile : g_gps_state.tiles) {
        if (tile.visible && tile.img_obj == NULL) {
            has_unloaded = true;
            break;
        }
    }
    
    if (!has_unloaded && g_gps_state.loading) {
        g_gps_state.loading = false;
        hide_loading();
        g_gps_state.initial_load_ms = 0;
        GPS_LOG("[GPS] All visible tiles loaded, hiding loading indicator\n");
    }
}

void tick_gps_update(bool allow_map_refresh)
{
    GPSData gps_data = getGPSData();
    
    static bool prev_has_fix = false;
    static bool prev_has_visible_map_data = false;
    static bool prev_gps_ready = false;
    static bool prev_sd_ready = false;
    static uint8_t prev_satellites = 0;
    static uint32_t last_title_update_ms = 0;
    const uint32_t TITLE_UPDATE_INTERVAL_MS = 30000;
    
    bool gps_ready = instance.isGPSReady();
    bool sd_ready = instance.isSDReady();
    uint32_t now_ms = millis();
    
    bool gps_state_changed = false;
    if (gps_data.valid) {
        double new_lat = gps_data.lat;
        double new_lng = gps_data.lng;
        
        if (!g_gps_state.has_fix || 
            fabs(new_lat - g_gps_state.lat) > 0.0001 || 
            fabs(new_lng - g_gps_state.lng) > 0.0001) {
            g_gps_state.lat = new_lat;
            g_gps_state.lng = new_lng;
            bool just_got_fix = !g_gps_state.has_fix;
            g_gps_state.has_fix = true;
            gps_state_changed = true;
            
            if (just_got_fix && g_gps_state.zoom_level == 0) {
                g_gps_state.zoom_level = DEFAULT_ZOOM;
                
                g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
                g_gps_state.last_resolution_lat = g_gps_state.lat;
                update_resolution_display();
            }
            
            if (allow_map_refresh) {
                g_gps_state.pan_x = 0;
                g_gps_state.pan_y = 0;
                
                g_gps_state.last_resolution_lat = g_gps_state.lat;
                update_map_tiles(false);
            }
        }
    } else {
        if (g_gps_state.has_fix) {
            g_gps_state.has_fix = false;
            g_gps_state.zoom_level = 0;
            
            g_gps_state.lat = DEFAULT_LAT;
            g_gps_state.lng = DEFAULT_LNG;
            
            g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
            g_gps_state.last_resolution_lat = g_gps_state.lat;
            update_resolution_display();
            gps_state_changed = true;
            
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
}
