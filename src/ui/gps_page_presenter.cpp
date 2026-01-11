#include "gps_page_presenter.h"
#include "gps_state.h"
#include "gps_page_components.h"
#include "board/TLoRaPagerBoard.h"
#include "gps/GPS.h"
#include "lvgl.h"
#include <Arduino.h>
#include <cmath>
#include <cstdio>

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

// Cached state for title/status updates
static struct {
    bool cached_has_fix;
    int cached_zoom;
    bool cached_sd_ready;
    bool cached_gps_ready;
    bool cached_has_map_data;
    uint8_t cached_satellites;
    bool initialized;
} last_status_state = {false, 0, false, false, false, 0, false};

// Helper function to calculate map resolution
static double calculate_map_resolution(int zoom, double lat)
{
    const double MAX_LAT = 85.05112878;
    double lat_clamped = lat;
    if (lat_clamped > MAX_LAT) lat_clamped = MAX_LAT;
    if (lat_clamped < -MAX_LAT) lat_clamped = -MAX_LAT;
    
    double resolution_equator = 156543.03392 / pow(2.0, zoom);
    
    double lat_rad = lat_clamped * M_PI / 180.0;
    double resolution = resolution_equator * cos(lat_rad);
    
    return resolution;
}

void update_resolution_display()
{
    if (g_gps_state.resolution_label == NULL) {
        return;
    }
    
    double lat = g_gps_state.has_fix ? g_gps_state.lat : 51.5074;
    
    double resolution_m = calculate_map_resolution(g_gps_state.zoom_level, lat);
    
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
    bool sd_ready = instance.isSDReady();
    bool gps_ready = instance.isGPSReady();
    GPSData gps_data = getGPSData();
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
    
    if (g_gps_state.page != NULL && g_gps_state.menu != NULL) {
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
        
        GPS_LOG("[GPS] Setting page title to: '%s' (page=%p, menu=%p)\n", 
                title_buffer, g_gps_state.page, g_gps_state.menu);
        
        lv_menu_set_page_title(g_gps_state.page, title_buffer);
        
        lv_obj_t* header = lv_menu_get_main_header(g_gps_state.menu);
        if (header != NULL) {
            GPS_LOG("[GPS] Found main header: %p\n", header);
            
            uint32_t child_cnt = lv_obj_get_child_cnt(header);
            GPS_LOG("[GPS] Header has %lu children\n", child_cnt);
            for (uint32_t i = 0; i < child_cnt; i++) {
                lv_obj_t* child = lv_obj_get_child(header, i);
                const lv_obj_class_t* obj_class = lv_obj_get_class(child);
                GPS_LOG("[GPS] Child %lu: %p, class=%s\n", i, child, obj_class ? obj_class->name : "NULL");
                if (lv_obj_check_type(child, &lv_label_class)) {
                    const char* old_text = lv_label_get_text(child);
                    GPS_LOG("[GPS] Found label child %lu: %p, old_text='%s', updating to '%s'\n", 
                           i, child, old_text ? old_text : "NULL", title_buffer);
                    lv_label_set_text(child, title_buffer);
                    lv_obj_invalidate(child);
                }
            }
        } else {
            GPS_LOG("[GPS] Main header is NULL\n");
        }
        
        uint32_t page_child_cnt = lv_obj_get_child_cnt(g_gps_state.page);
        GPS_LOG("[GPS] Page has %lu direct children\n", page_child_cnt);
        for (uint32_t i = 0; i < page_child_cnt; i++) {
            lv_obj_t* page_child = lv_obj_get_child(g_gps_state.page, i);
            const lv_obj_class_t* obj_class = lv_obj_get_class(page_child);
            GPS_LOG("[GPS] Page child %lu: %p, class=%s\n", i, page_child, obj_class ? obj_class->name : "NULL");
            
            if (page_child == g_gps_state.map) {
                GPS_LOG("[GPS] Skipping map container (child %lu) to avoid updating tile labels\n", i);
                continue;
            }
            
            if (lv_obj_check_type(page_child, &lv_label_class)) {
                const char* old_text = lv_label_get_text(page_child);
                
                if (old_text != NULL) {
                    int z, x, y;
                    if (sscanf(old_text, "%d/%d/%d", &z, &x, &y) == 3) {
                        GPS_LOG("[GPS] Skipping tile placeholder label: '%s'\n", old_text);
                        continue;
                    }
                }
                GPS_LOG("[GPS] Found label in page child %lu: %p, old_text='%s', updating to '%s'\n", 
                       i, page_child, old_text ? old_text : "NULL", title_buffer);
                lv_label_set_text(page_child, title_buffer);
                lv_obj_invalidate(page_child);
            }
            
            if (page_child != g_gps_state.map) {
                uint32_t nested_cnt = lv_obj_get_child_cnt(page_child);
                for (uint32_t j = 0; j < nested_cnt; j++) {
                    lv_obj_t* nested = lv_obj_get_child(page_child, j);
                    if (lv_obj_check_type(nested, &lv_label_class)) {
                        const char* old_text = lv_label_get_text(nested);
                        
                        if (old_text != NULL) {
                            int z, x, y;
                            if (sscanf(old_text, "%d/%d/%d", &z, &x, &y) == 3) {
                                GPS_LOG("[GPS] Skipping tile placeholder label (nested): '%s'\n", old_text);
                                continue;
                            }
                        }
                        GPS_LOG("[GPS] Found nested label in page child %lu/%lu: %p, old_text='%s', updating to '%s'\n", 
                               i, j, nested, old_text ? old_text : "NULL", title_buffer);
                        lv_label_set_text(nested, title_buffer);
                        lv_obj_invalidate(nested);
                    }
                }
            }
        }
        
        lv_obj_update_layout(g_gps_state.menu);
        lv_obj_update_layout(g_gps_state.page);
        if (header != NULL) {
            lv_obj_update_layout(header);
            lv_obj_invalidate(header);
        }
        lv_obj_invalidate(g_gps_state.menu);
        lv_obj_invalidate(g_gps_state.page);
        
        GPS_LOG("[GPS] Page title updated, invalidated menu and page\n");
    } else {
        GPS_LOG("[GPS] ERROR: page=%p or menu=%p is NULL, cannot update title\n", 
                g_gps_state.page, g_gps_state.menu);
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
