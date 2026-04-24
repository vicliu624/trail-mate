/**
 * @file gps_state.h
 * @brief GPS page state structure definition
 */

#ifndef GPS_STATE_H
#define GPS_STATE_H

#include "ui/screens/gps/gps_constants.h"
#include "ui/screens/gps/gps_modal.h"
#include "lvgl.h"
#include "ui/widgets/map/map_tiles.h"
#include "ui/widgets/top_bar.h"
#include <cstdint>
#include <string>
#include <vector>

enum class GpsEditMode : uint8_t
{
    None = 0,
    PanH = 1,
    PanV = 2,
    ZoomPopup = 3,
};

struct GPSPageState
{
    struct TrackOverlayPoint
    {
        double lat = 0.0;
        double lng = 0.0;
    };

    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* menu = nullptr;
    lv_obj_t* page = nullptr;
    lv_obj_t* map = nullptr;
    lv_obj_t* resolution_label = nullptr;
    lv_obj_t* altitude_label = nullptr;
    lv_obj_t* panel = nullptr;
    lv_obj_t* member_panel = nullptr;
    lv_obj_t* zoom = nullptr;
    lv_obj_t* pos = nullptr;
    lv_obj_t* pan_h = nullptr;
    lv_obj_t* pan_v = nullptr;
    lv_obj_t* tracker_btn = nullptr;
    lv_obj_t* layer_btn = nullptr;
    lv_obj_t* route_btn = nullptr;
    lv_obj_t* pan_h_indicator = nullptr;
    lv_obj_t* pan_v_indicator = nullptr;
    lv_obj_t* popup_label = nullptr;
    lv_obj_t* popup_roller = nullptr;
    lv_obj_t* popup_apply_btn = nullptr;
    lv_obj_t* popup_cancel_btn = nullptr;
    lv_obj_t* loading_msgbox = nullptr;
    lv_obj_t* toast_msgbox = nullptr;
    lv_timer_t* toast_timer = nullptr;
    lv_obj_t* gps_marker = nullptr;
    ui::widgets::TopBar top_bar;

    int zoom_level = gps_ui::kDefaultZoom;
    double lat = 0.0;
    double lng = 0.0;
    bool has_fix = false;
    int pan_x = 0;
    int pan_y = 0;
    bool follow_position = true;

    MapAnchor anchor = {0};
    std::vector<MapTile> tiles;
    TileContext tile_ctx;

    uint32_t initial_load_ms = 0;
    bool initial_tiles_loaded = false;

    Modal zoom_modal;
    Modal tracker_modal;
    Modal layer_modal;
    Modal route_modal;
    int popup_zoom = gps_ui::kDefaultZoom;
    bool zoom_win_cb_bound = false;

    lv_timer_t* timer = nullptr;
    lv_timer_t* loader_timer = nullptr;
    lv_timer_t* title_timer = nullptr;
    std::vector<lv_timer_t*> timers;
    lv_indev_t* encoder = nullptr;
    lv_group_t* app_group = nullptr;

    bool alive = false;
    bool delete_hook_bound = false;
    bool exiting = false;
    bool has_map_data = false;
    bool has_visible_map_data = false;

    bool tracker_overlay_active = false;
    bool tracker_draw_cb_bound = false;
    std::string tracker_file{};
    std::vector<TrackOverlayPoint> tracker_points;
    std::vector<lv_point_t> tracker_screen_points;

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

    struct TeamSignalMarker
    {
        uint32_t member_id = 0;
        int32_t lat_e7 = 0;
        int32_t lon_e7 = 0;
        uint32_t ts = 0;
        uint8_t icon_id = 0;
        lv_obj_t* obj = nullptr;
        lv_obj_t* label = nullptr;
    };
    std::vector<TeamSignalMarker> team_signal_markers;
    uint32_t team_signal_marker_last_ms = 0;

    GpsEditMode edit_mode = GpsEditMode::None;

#ifdef USING_INPUT_DEV_TOUCHPAD
    struct TouchPanState
    {
        bool pressed = false;
        bool dragging = false;
        lv_point_t start = {0, 0};
        lv_point_t last = {0, 0};
        int start_pan_x = 0;
        int start_pan_y = 0;
    } touch_pan;
    lv_timer_t* touch_timer = nullptr;
#endif

    bool pending_refresh = false;
    double last_resolution_lat = 0.0;
    int last_resolution_zoom = -1;
};

extern GPSPageState g_gps_state;

#endif // GPS_STATE_H
