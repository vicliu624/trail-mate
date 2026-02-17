/**
 * @file gps_state.h
 * @brief GPS page state structure definition
 */

#ifndef GPS_STATE_H
#define GPS_STATE_H

#include "../../widgets/map/map_tiles.h"
#include "../../widgets/top_bar.h"
#include "gps_constants.h"
#include "gps_modal.h"
#include "lvgl.h"
#include <cstdint>
#include <string>
#include <vector>

/**
 * GPS Page State - consolidate all static state variables
 * Makes cleanup/exit logic simpler and prevents state leaks
 * Note: No macro aliases - use g_gps_state.member directly
 */
struct GPSPageState
{
    struct TrackOverlayPoint
    {
        double lat = 0.0;
        double lng = 0.0;
    };

    // UI refs
    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* menu = nullptr;
    lv_obj_t* page = nullptr;
    lv_obj_t* map = nullptr;
    lv_obj_t* resolution_label = nullptr; // Resolution display label (bottom-left)
    lv_obj_t* altitude_label = nullptr;   // Altitude display label (bottom-center)
    lv_obj_t* panel = nullptr;
    lv_obj_t* member_panel = nullptr;
    lv_obj_t* zoom = nullptr;
    lv_obj_t* pos = nullptr;
    lv_obj_t* pan_h = nullptr;           // Horizontal pan button
    lv_obj_t* pan_v = nullptr;           // Vertical pan button
    lv_obj_t* tracker_btn = nullptr;     // Tracker button
    lv_obj_t* layer_btn = nullptr;       // Map layer button
    lv_obj_t* route_btn = nullptr;       // Route focus button
    lv_obj_t* pan_h_indicator = nullptr; // Horizontal pan indicator (line with arrows at bottom)
    lv_obj_t* pan_v_indicator = nullptr; // Vertical pan indicator (line with arrows on right)
    lv_obj_t* popup_label = nullptr;     // zoom_popup_label
    lv_obj_t* loading_msgbox = nullptr;
    lv_obj_t* toast_msgbox = nullptr;  // Toast notification message box
    lv_timer_t* toast_timer = nullptr; // Timer to auto-hide toast
    lv_obj_t* gps_marker = nullptr;    // GPS position marker (rendered on map)
    ui::widgets::TopBar top_bar;       // Shared header

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
    TileContext tile_ctx; // Context for tile operations

    uint32_t initial_load_ms = 0;
    bool initial_tiles_loaded = false;

    // popup
    Modal zoom_modal;
    Modal tracker_modal;
    Modal layer_modal;
    int popup_zoom = gps_ui::kDefaultZoom;
    bool zoom_win_cb_bound = false;

    // misc
    lv_timer_t* timer = nullptr;        // Main timer for tile loading and GPS updates
    lv_timer_t* loader_timer = nullptr; // Tile loader timer (higher frequency)
    lv_timer_t* title_timer = nullptr;  // Separate timer for title updates (30s)
    std::vector<lv_timer_t*> timers;    // Lifetime-managed timers for this screen
    lv_indev_t* encoder = nullptr;
    lv_group_t* app_group = nullptr; // App-level focus group captured at enter

    // flags
    bool alive = false;                // Hard lifetime guard: false after root delete hook runs
    bool delete_hook_bound = false;    // Ensure root delete hook is only bound once
    bool exiting = false;              // Prevent re-entrant exit while async exit is pending
    bool has_map_data = false;         // Global: any tile ever loaded
    bool has_visible_map_data = false; // Viewport: current visible tiles have PNG

    // Tracker overlay
    bool tracker_overlay_active = false;
    bool tracker_draw_cb_bound = false;
    std::string tracker_file{};
    std::vector<TrackOverlayPoint> tracker_points;
    std::vector<lv_point_t> tracker_screen_points;

    // Route overlay (KML)
    bool route_overlay_active = false;
    bool route_draw_cb_bound = false;
    bool route_bbox_valid = false;
    std::string route_file{};
    std::vector<TrackOverlayPoint> route_points;
    std::vector<lv_point_t> route_screen_points;
    double route_min_lat = 0.0;
    double route_min_lng = 0.0;
    double route_max_lat = 0.0;
    double route_max_lng = 0.0;
    // Route overlay cache (avoid recomputing screen points on every draw)
    int route_cache_zoom = -1;
    int route_cache_pan_x = 0;
    int route_cache_pan_y = 0;
    bool route_cache_anchor_valid = false;
    int32_t route_cache_anchor_px_x = 0;
    int32_t route_cache_anchor_px_y = 0;
    int route_cache_anchor_screen_x = 0;
    int route_cache_anchor_screen_y = 0;
    int32_t route_cache_offset_x = 0;
    int32_t route_cache_offset_y = 0;
    int route_cache_map_w = 0;
    int route_cache_map_h = 0;
    uint32_t route_cache_point_count = 0;

    std::vector<lv_obj_t*> member_btns;
    std::vector<uint32_t> member_btn_ids;
    uint32_t member_list_hash = 0;
    uint32_t member_panel_last_ms = 0;
    uint32_t selected_member_id = 0xFFFFFFFFu;

    struct TeamMarker
    {
        uint32_t member_id = 0;
        int32_t lat_e7 = 0;
        int32_t lon_e7 = 0;
        uint32_t ts = 0;
        uint32_t color = 0;
        lv_obj_t* obj = nullptr;
        lv_obj_t* label = nullptr;
    };
    std::vector<TeamMarker> team_markers;
    uint32_t team_marker_last_ms = 0;

    // pan button editing state (for toggle behavior) - DEPRECATED, use edit_mode instead
    bool pan_h_editing = false; // Horizontal pan button in editing mode (rotary scrolls map)
    bool pan_v_editing = false; // Vertical pan button in editing mode (rotary scrolls map)

    // Edit mode state machine
    uint8_t edit_mode = 0; // 0=None, 1=PanH, 2=PanV, 3=ZoomPopup

    // refresh optimization
    bool pending_refresh = false;     // Flag to indicate map needs refresh (for batched updates)
    double last_resolution_lat = 0.0; // Last latitude used for resolution calculation
    int last_resolution_zoom = -1;    // Last zoom level used for resolution calculation
};

// Global state instance (defined in gps_page.cpp)
extern GPSPageState g_gps_state;

#endif // GPS_STATE_H
