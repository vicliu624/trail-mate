
#include "screens/gps/gps_state.h"
#include "screens/gps/gps_modal.h"
#include "widgets/map/map_tiles.h"
#include "screens/gps/gps_page_input.h"
#include "screens/gps/gps_page_components.h"
#include "screens/gps/gps_page_map.h"
#include "screens/gps/gps_constants.h"
#include "ui_common.h"
#include "board/BoardBase.h"
#include "display/DisplayInterface.h"
#include "gps/GPS.h"
#include <Arduino.h>
#include <cmath>


#include "../gps/gps_service_api.h"

using GPSData = gps::GpsState;
extern void updateUserActivity();



#define GPS_DEBUG 1  // Enable debug logging
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

void ui_gps_exit(lv_obj_t *parent);

static void gps_top_bar_back(void* /*user_data*/)
{
    ui_gps_exit(nullptr);
    menu_show();
}


GPSPageState g_gps_state;
// Root container for GPS UI (equivalent to chat screens' container_)
static lv_obj_t* gps_root = nullptr;

bool isGPSLoadingTiles()
{
    return g_gps_state.loading;
}

static void title_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    GPS_LOG("[GPS] Title update timer fired (30s) - calling update_title_and_status\n");
    
    reset_title_status_cache();
    update_title_and_status();
}


static void gps_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    
    if (g_gps_state.pending_refresh) {
        g_gps_state.pending_refresh = false;
        update_map_tiles(false);
    }
    
    tick_loader();  
    
    if (g_gps_state.zoom_modal.is_open()) {
        tick_gps_update(false);  
        return;
    }
    
    
    tick_gps_update(true);  
}

// GPS page uses shared TopBar with its own back callback; no lv_menu back handler required.

void ui_gps_enter(lv_obj_t *parent)
{
    GPS_LOG("[GPS] Entering GPS page, SD ready: %d, GPS ready: %d\n",
            board.isSDReady(), board.isGPSReady());
    
    // CRITICAL: 重置 control tag 池，避免溢出
    reset_control_tags();
    
    g_gps_state = GPSPageState{};  
    
    if (gps_root != nullptr) {
        lv_obj_del(gps_root);
        gps_root = nullptr;
    }
    
    // Root container: match chat screens' layout style
    gps_root = lv_obj_create(parent);
    lv_obj_set_size(gps_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(gps_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(gps_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gps_root, 0, 0);
    lv_obj_set_style_pad_all(gps_root, 0, 0);
    lv_obj_set_style_radius(gps_root, 0, 0);
    lv_obj_set_flex_flow(gps_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(gps_root, 0, 0);
    lv_obj_clear_flag(gps_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(gps_root, LV_SCROLLBAR_MODE_OFF);
    
    ::init_tile_context(g_gps_state.tile_ctx, 
                       nullptr,  
                       &g_gps_state.anchor, 
                       &g_gps_state.tiles, 
                       &g_gps_state.has_map_data, 
                       &g_gps_state.has_visible_map_data);
    
    // Shared top bar: same component as chat screens
    lv_obj_t* header = lv_obj_create(gps_root);
    lv_obj_set_size(header, LV_PCT(100), ::ui::widgets::kTopBarHeight);
    lv_obj_set_style_bg_color(header, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(g_gps_state.top_bar, header, cfg);
    ::ui::widgets::top_bar_set_title(g_gps_state.top_bar, "GPS");
    ::ui::widgets::top_bar_set_back_callback(g_gps_state.top_bar, gps_top_bar_back, nullptr);
    
    // Content area under top bar: flex-grow = 1
    lv_obj_t* content = lv_obj_create(gps_root);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_color(content, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    // Map container fills the content area
    g_gps_state.page = content; // Keep for compatibility with gps_page_components (loading/toast containers)
    g_gps_state.map = lv_obj_create(content);
    lv_obj_set_size(g_gps_state.map, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_gps_state.map, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_gps_state.map, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_gps_state.map, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(g_gps_state.map, 0, LV_PART_MAIN);  
    lv_obj_set_style_pad_all(g_gps_state.map, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_all(g_gps_state.map, 0, LV_PART_MAIN);
    lv_obj_clear_flag(g_gps_state.map, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_gps_state.map, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_gps_state.map, LV_OBJ_FLAG_CLICKABLE);
    set_control_id(g_gps_state.map, ControlId::Map);
    
    
    g_gps_state.tile_ctx.map_container = g_gps_state.map;
    
    extern lv_group_t *app_g;

    g_gps_state.status = lv_label_create(content);
    lv_obj_set_style_bg_color(g_gps_state.status, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_gps_state.status, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_gps_state.status, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_gps_state.status, 8, LV_PART_MAIN);
    lv_obj_align(g_gps_state.status, LV_ALIGN_TOP_LEFT, 10, 10);

    
    g_gps_state.resolution_label = lv_label_create(g_gps_state.map);  
    lv_obj_set_style_text_color(g_gps_state.resolution_label, lv_color_hex(0x808080), LV_PART_MAIN);  
    lv_obj_set_style_text_font(g_gps_state.resolution_label, &lv_font_montserrat_16, LV_PART_MAIN);  
    lv_obj_set_style_text_opa(g_gps_state.resolution_label, LV_OPA_COVER, LV_PART_MAIN);
    
    
    lv_obj_set_style_bg_opa(g_gps_state.resolution_label, LV_OPA_TRANSP, LV_PART_MAIN);  
    lv_obj_set_style_pad_all(g_gps_state.resolution_label, 4, LV_PART_MAIN);  
    lv_obj_align(g_gps_state.resolution_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);  
    lv_label_set_text(g_gps_state.resolution_label, "");  

    // GPS marker will be created when position button is clicked
    // Don't create it here - it should be created on demand

    
    
    g_gps_state.panel = lv_obj_create(g_gps_state.map);
    lv_obj_set_style_bg_opa(g_gps_state.panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_gps_state.panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_gps_state.panel, 0, LV_PART_MAIN);
    lv_obj_set_style_margin_all(g_gps_state.panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_gps_state.panel, 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_gps_state.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_gps_state.panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
    lv_obj_set_size(g_gps_state.panel, 85, LV_SIZE_CONTENT);
    lv_obj_clear_flag(g_gps_state.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_gps_state.panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_align(g_gps_state.panel, LV_ALIGN_TOP_RIGHT, 0, 3);
    
    
    g_gps_state.zoom = lv_btn_create(g_gps_state.panel);
    style_control_button(g_gps_state.zoom, lv_color_hex(0xF4C77A));
    set_control_id(g_gps_state.zoom, ControlId::ZoomBtn);
    lv_obj_add_event_cb(g_gps_state.zoom, on_ui_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.zoom, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.zoom, on_ui_event, LV_EVENT_ROTARY, NULL);
    
    
    lv_obj_t* zoom_lbl = lv_label_create(g_gps_state.zoom);
    lv_label_set_text(zoom_lbl, "[Z]oom");
    lv_obj_set_style_text_color(zoom_lbl, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_text_font(zoom_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(zoom_lbl);
    
    
    g_gps_state.pos = lv_btn_create(g_gps_state.panel);
    style_control_button(g_gps_state.pos, lv_color_hex(0xF4C77A));
    set_control_id(g_gps_state.pos, ControlId::PosBtn);
    lv_obj_add_event_cb(g_gps_state.pos, on_ui_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pos, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.pos, on_ui_event, LV_EVENT_ROTARY, NULL);
    
    
    lv_obj_t* pos_lbl = lv_label_create(g_gps_state.pos);
    lv_label_set_text(pos_lbl, "[P]osition");
    lv_obj_set_style_text_color(pos_lbl, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_text_font(pos_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(pos_lbl);
    
    
    g_gps_state.pan_h = lv_btn_create(g_gps_state.panel);
    GPS_LOG("[GPS] Created pan_h button at %p\n", g_gps_state.pan_h);
    style_control_button(g_gps_state.pan_h, lv_color_hex(0xF4C77A));
    set_control_id(g_gps_state.pan_h, ControlId::PanHBtn);
    lv_obj_add_event_cb(g_gps_state.pan_h, on_ui_event, LV_EVENT_ROTARY, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h, on_ui_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h, on_ui_event, LV_EVENT_KEY, NULL);
    
    
    lv_obj_t* pan_h_lbl = lv_label_create(g_gps_state.pan_h);
    lv_label_set_text(pan_h_lbl, "[H]oriz");
    lv_obj_set_style_text_color(pan_h_lbl, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_text_font(pan_h_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(pan_h_lbl);
    
    
    g_gps_state.pan_v = lv_btn_create(g_gps_state.panel);
    GPS_LOG("[GPS] Created pan_v button at %p\n", g_gps_state.pan_v);
    style_control_button(g_gps_state.pan_v, lv_color_hex(0xF4C77A));
    set_control_id(g_gps_state.pan_v, ControlId::PanVBtn);
    lv_obj_add_event_cb(g_gps_state.pan_v, on_ui_event, LV_EVENT_ROTARY, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v, on_ui_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v, on_ui_event, LV_EVENT_KEY, NULL);
    
    
    lv_obj_t* pan_v_lbl = lv_label_create(g_gps_state.pan_v);
    lv_label_set_text(pan_v_lbl, "[V]ert");
    lv_obj_set_style_text_color(pan_v_lbl, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_text_font(pan_v_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(pan_v_lbl);
    
    
    lv_group_add_obj(app_g, g_gps_state.zoom);
    lv_group_add_obj(app_g, g_gps_state.pos);
    lv_group_add_obj(app_g, g_gps_state.pan_h);
    lv_group_add_obj(app_g, g_gps_state.pan_v);
    
    
    GPS_LOG("[GPS] Button addresses: zoom=%p, pos=%p, pan_h=%p, pan_v=%p\n",
            g_gps_state.zoom, g_gps_state.pos, g_gps_state.pan_h, g_gps_state.pan_v);
    
    
    
    lv_obj_add_event_cb(g_gps_state.map, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.map, on_ui_event, LV_EVENT_ROTARY, NULL);
    
    
    lv_obj_move_foreground(g_gps_state.panel);
    if (g_gps_state.resolution_label != NULL) {
        lv_obj_move_foreground(g_gps_state.resolution_label);
    }
    

    GPSData gps_data = gps::gps_get_data();
    GPS_LOG("[GPS] Initial GPS data: valid=%d, lat=%.6f, lng=%.6f\n",
            gps_data.valid, gps_data.lat, gps_data.lng);
    
    if (gps_data.valid) {
        g_gps_state.lat = gps_data.lat;
        g_gps_state.lng = gps_data.lng;
        g_gps_state.has_fix = true;
        GPS_LOG("[GPS] Initialized with GPS fix: lat=%.6f, lng=%.6f, zoom=%d\n",
                g_gps_state.lat, g_gps_state.lng, g_gps_state.zoom_level);
    } else {
        g_gps_state.zoom_level = 0;
        
        g_gps_state.lat = gps_ui::kDefaultLat;
        g_gps_state.lng = gps_ui::kDefaultLng;
        g_gps_state.has_fix = false;
        GPS_LOG("[GPS] No GPS fix, using London as default center (lat=%.4f, lng=%.4f, zoom=0)\n",
                g_gps_state.lat, g_gps_state.lng);
    }
    
    
    g_gps_state.has_map_data = false;
    g_gps_state.has_visible_map_data = false;
    
    g_gps_state.pan_x = 0;
    g_gps_state.pan_y = 0;
    
    
    g_gps_state.pan_h_editing = false;
    g_gps_state.pan_v_editing = false;
    
    
    g_gps_state.pending_refresh = false;
    g_gps_state.last_resolution_lat = 0.0;
    g_gps_state.last_resolution_zoom = -1;
    
    
    hide_pan_h_indicator();
    hide_pan_v_indicator();
    
    extern lv_group_t *app_g;
    if (app_g != NULL) {
        lv_group_set_editing(app_g, false);
    }
    
    
    
    g_gps_state.anchor.valid = false;
    g_gps_state.initial_load_ms = 0;
    
    
    g_gps_state.tiles.clear();
    g_gps_state.tiles.reserve(TILE_RECORD_LIMIT);
    
    
    reset_title_status_cache();
    update_title_and_status();
    update_zoom_btn();
    
    g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
    g_gps_state.last_resolution_lat = g_gps_state.lat;
    update_resolution_display();  
    
    
    g_gps_state.initial_tiles_loaded = false;
    
    
    if (g_gps_state.map != NULL && g_gps_state.tile_ctx.map_container == g_gps_state.map) {
        update_map_tiles(false);  
    } else {
        GPS_LOG("[GPS] WARNING: map=%p, tile_ctx.map_container=%p, skipping initial tile calculation\n",
                g_gps_state.map, g_gps_state.tile_ctx.map_container);
    }

    set_default_group(app_g);
    
    
    
    lv_group_set_editing(app_g, false);
    
    
    
    g_gps_state.timer = lv_timer_create(gps_update_timer_cb, 33, NULL);
    
    
    g_gps_state.title_timer = lv_timer_create(title_update_timer_cb, 30000, NULL);
    
    
    update_title_and_status();
    
    GPS_LOG("[GPS] GPS page initialized: main_timer=%p, title_timer=%p\n", 
            g_gps_state.timer, g_gps_state.title_timer);
}

void ui_gps_exit(lv_obj_t *parent)
{
    (void)parent;

    GPS_LOG("[GPS] Exiting GPS page\n");
    
    // Clean up GPS marker
    extern void hide_gps_marker();
    if (g_gps_state.gps_marker != NULL) {
        lv_obj_del(g_gps_state.gps_marker);
        g_gps_state.gps_marker = nullptr;
    }
    
    
    if (g_gps_state.timer != NULL) {
        lv_timer_del(g_gps_state.timer);
        g_gps_state.timer = nullptr;
    }
    if (g_gps_state.title_timer != NULL) {
        lv_timer_del(g_gps_state.title_timer);
        g_gps_state.title_timer = nullptr;
    }
    if (g_gps_state.toast_timer != NULL) {
        lv_timer_del(g_gps_state.toast_timer);
        g_gps_state.toast_timer = nullptr;
    }
    
    
    hide_pan_h_indicator();
    hide_pan_v_indicator();
    g_gps_state.pan_h = nullptr;
    g_gps_state.pan_v = nullptr;
    
    
    ::cleanup_tiles(g_gps_state.tile_ctx);
    
    
    hide_loading();
    hide_toast();
    hide_zoom_popup();
    
    
    if (g_gps_state.zoom_modal.group != NULL) {
        lv_group_del(g_gps_state.zoom_modal.group);
        g_gps_state.zoom_modal.group = nullptr;
    }
    
    
    if (gps_root != nullptr) {
        lv_obj_del(gps_root);
        gps_root = nullptr;
    }

    // Reset GPS page state (all pointers and flags)
    GPSPageState empty = {};
    g_gps_state = empty;
    
    
    reset_title_status_cache();
    
    
    extern lv_group_t *app_g;
    bind_encoder_to_group(app_g);
    
    GPS_LOG("[GPS] GPS page exited, all state cleared\n");
}
