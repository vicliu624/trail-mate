/**
 * @file gps_state.h
 * @brief GPS page state structure definition
 */

#ifndef GPS_STATE_H
#define GPS_STATE_H

#include "lvgl.h"
#include "../../widgets/map/map_tiles.h"
#include "gps_modal.h"
#include "gps_constants.h"
#include "../../widgets/top_bar.h"
#include <vector>
#include <cstdint>
#include <string>

/**
 * GPS Page State - consolidate all static state variables
 * Makes cleanup/exit logic simpler and prevents state leaks
 * Note: No macro aliases - use g_gps_state.member directly
 */
struct GPSPageState {
    struct TrackOverlayPoint {
        double lat = 0.0;
        double lng = 0.0;
    };

    // UI refs
    lv_obj_t *menu = nullptr;
    lv_obj_t *page = nullptr;
    lv_obj_t *map = nullptr;
    lv_obj_t *status = nullptr;
    lv_obj_t *resolution_label = nullptr;  // Resolution display label (bottom-left)
    lv_obj_t *panel = nullptr;
    lv_obj_t *zoom = nullptr;
    lv_obj_t *pos = nullptr;
    lv_obj_t *pan_h = nullptr;  // Horizontal pan button
    lv_obj_t *pan_v = nullptr;  // Vertical pan button
    lv_obj_t *tracker_btn = nullptr;  // Tracker button
    lv_obj_t *pan_h_indicator = nullptr;  // Horizontal pan indicator (line with arrows at bottom)
    lv_obj_t *pan_v_indicator = nullptr;  // Vertical pan indicator (line with arrows on right)
    lv_obj_t *popup_label = nullptr;  // zoom_popup_label
    lv_obj_t *loading_msgbox = nullptr;
    lv_obj_t *toast_msgbox = nullptr;  // Toast notification message box
    lv_timer_t* toast_timer = nullptr;  // Timer to auto-hide toast
    lv_obj_t *gps_marker = nullptr;  // GPS position marker (rendered on map)
    ui::widgets::TopBar top_bar;  // Shared header

    
    // GPS/map
    int zoom_level = gps_ui::kDefaultZoom;
    double lat = 0.0;
    double lng = 0.0;
    bool has_fix = false;
    int pan_x = 0;
    int pan_y = 0;
    
    // tile/cache (actual storage, not pointers)
    MapAnchor anchor = {0};
    std::vector<MapTile> tiles;
    TileContext tile_ctx;  // Context for tile operations
    
    // loader
    bool loading = false;
    uint32_t initial_load_ms = 0;
    bool initial_tiles_loaded = false;
    
    // popup
    Modal zoom_modal;
    Modal tracker_modal;
    int popup_zoom = gps_ui::kDefaultZoom;
    bool zoom_win_cb_bound = false;
    
    // misc
    lv_timer_t* timer = nullptr;  // Main timer for tile loading and GPS updates
    lv_timer_t* title_timer = nullptr;  // Separate timer for title updates (30s)
    lv_indev_t* encoder = nullptr;
    
    // flags
    bool has_map_data = false;  // Global: any tile ever loaded
    bool has_visible_map_data = false;  // Viewport: current visible tiles have PNG

    // Tracker overlay
    bool tracker_overlay_active = false;
    bool tracker_draw_cb_bound = false;
    std::string tracker_file{};
    std::vector<TrackOverlayPoint> tracker_points;
    std::vector<lv_point_t> tracker_screen_points;
    
    // pan button editing state (for toggle behavior) - DEPRECATED, use edit_mode instead
    bool pan_h_editing = false;  // Horizontal pan button in editing mode (rotary scrolls map)
    bool pan_v_editing = false;  // Vertical pan button in editing mode (rotary scrolls map)
    
    // Edit mode state machine
    uint8_t edit_mode = 0;  // 0=None, 1=PanH, 2=PanV, 3=ZoomPopup
    
    // Dirty flags for UI updates
    uint8_t dirty_map : 1;
    uint8_t dirty_title : 1;
    uint8_t dirty_status : 1;
    uint8_t dirty_resolution : 1;
    
    // refresh optimization
    bool pending_refresh = false;  // Flag to indicate map needs refresh (for batched updates)
    double last_resolution_lat = 0.0;  // Last latitude used for resolution calculation
    int last_resolution_zoom = -1;  // Last zoom level used for resolution calculation
};

// Global state instance (defined in gps_page.cpp)
extern GPSPageState g_gps_state;

#endif // GPS_STATE_H
