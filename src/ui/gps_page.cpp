

#include "gps_page.h"
#include "gps_state.h"
#include "gps_modal.h"
#include "map_tiles.h"
#include "gps_page_input.h"
#include "gps_page_components.h"
#include "gps_page_presenter.h"
#include "gps_page_map.h"
#include "ui_common.h"
#include "board/TLoRaPagerBoard.h"
#include "display/DisplayInterface.h"
#include "gps/GPS.h"
#include <Arduino.h>
#include <cmath>


struct GPSData {
    double lat;
    double lng;
    uint8_t satellites;
    bool valid;
    uint32_t age;
};
extern GPSData getGPSData();
extern void updateUserActivity();


#define MAP_PAN_STEP 32
#define DEFAULT_ZOOM 12
#define MIN_ZOOM 0
#define MAX_ZOOM 18


#define DEFAULT_LAT 51.5074
#define DEFAULT_LNG -0.1278


#define GPS_DEBUG 1  // Enable debug logging
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif


GPSPageState g_gps_state;


bool indev_is_pressed(lv_event_t *e);

bool isGPSLoadingTiles()
{
    return g_gps_state.loading;
}


bool indev_is_pressed(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev) return false;
    return lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
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

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(g_gps_state.menu, obj)) {
        ui_gps_exit(NULL);
        menu_show();
    }
}

void ui_gps_enter(lv_obj_t *parent)
{
    GPS_LOG("[GPS] Entering GPS page, SD ready: %d, GPS ready: %d\n", 
            instance.isSDReady(), instance.isGPSReady());
    
    // CRITICAL: 重置 control tag 池，避免溢出
    reset_control_tags();
    
    g_gps_state = GPSPageState{};  
    
    
    ::init_tile_context(g_gps_state.tile_ctx, 
                       nullptr,  
                       &g_gps_state.anchor, 
                       &g_gps_state.tiles, 
                       &g_gps_state.has_map_data, 
                       &g_gps_state.has_visible_map_data);
    
    g_gps_state.menu = create_menu(parent, back_event_handler);
    g_gps_state.page = lv_menu_page_create(g_gps_state.menu, "GPS");
    
    
    lv_obj_clear_flag(g_gps_state.page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(g_gps_state.page, LV_SCROLLBAR_MODE_OFF);

    g_gps_state.map = lv_obj_create(g_gps_state.page);
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

    g_gps_state.status = lv_label_create(g_gps_state.page);
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

    lv_obj_t *marker = lv_label_create(g_gps_state.page);
    lv_label_set_text(marker, LV_SYMBOL_GPS);
    lv_obj_set_style_text_color(marker, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_text_font(marker, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_bg_color(marker, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(marker, 4, LV_PART_MAIN);
    lv_obj_align(marker, LV_ALIGN_CENTER, 0, 0);

    
    
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
    lv_obj_align(g_gps_state.panel, LV_ALIGN_TOP_RIGHT, 0, 0);
    
    
    g_gps_state.zoom = lv_btn_create(g_gps_state.panel);
    style_control_button(g_gps_state.zoom, lv_color_hex(0xFF6600));
    set_control_id(g_gps_state.zoom, ControlId::ZoomBtn);
    lv_obj_add_event_cb(g_gps_state.zoom, on_ui_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.zoom, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.zoom, on_ui_event, LV_EVENT_ROTARY, NULL);
    
    
    lv_obj_t* zoom_lbl = lv_label_create(g_gps_state.zoom);
    lv_label_set_text(zoom_lbl, "[Z]oom");
    lv_obj_set_style_text_color(zoom_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(zoom_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(zoom_lbl);
    
    
    g_gps_state.pos = lv_btn_create(g_gps_state.panel);
    style_control_button(g_gps_state.pos, lv_color_hex(0x87CEEB));
    set_control_id(g_gps_state.pos, ControlId::PosBtn);
    lv_obj_add_event_cb(g_gps_state.pos, on_ui_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pos, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.pos, on_ui_event, LV_EVENT_ROTARY, NULL);
    
    
    lv_obj_t* pos_lbl = lv_label_create(g_gps_state.pos);
    lv_label_set_text(pos_lbl, "[P]osition");
    lv_obj_set_style_text_color(pos_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(pos_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(pos_lbl);
    
    
    g_gps_state.pan_h = lv_btn_create(g_gps_state.panel);
    GPS_LOG("[GPS] Created pan_h button at %p\n", g_gps_state.pan_h);
    style_control_button(g_gps_state.pan_h, lv_color_hex(0x90EE90));
    set_control_id(g_gps_state.pan_h, ControlId::PanHBtn);
    lv_obj_add_event_cb(g_gps_state.pan_h, on_ui_event, LV_EVENT_ROTARY, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h, on_ui_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h, on_ui_event, LV_EVENT_KEY, NULL);
    
    
    lv_obj_t* pan_h_lbl = lv_label_create(g_gps_state.pan_h);
    lv_label_set_text(pan_h_lbl, "[H]oriz");
    lv_obj_set_style_text_color(pan_h_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(pan_h_lbl, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(pan_h_lbl);
    
    
    g_gps_state.pan_v = lv_btn_create(g_gps_state.panel);
    GPS_LOG("[GPS] Created pan_v button at %p\n", g_gps_state.pan_v);
    style_control_button(g_gps_state.pan_v, lv_color_hex(0xFFCC66));
    set_control_id(g_gps_state.pan_v, ControlId::PanVBtn);
    lv_obj_add_event_cb(g_gps_state.pan_v, on_ui_event, LV_EVENT_ROTARY, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v, on_ui_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v, on_ui_event, LV_EVENT_KEY, NULL);
    
    
    lv_obj_t* pan_v_lbl = lv_label_create(g_gps_state.pan_v);
    lv_label_set_text(pan_v_lbl, "[V]ert");
    lv_obj_set_style_text_color(pan_v_lbl, lv_color_white(), LV_PART_MAIN);
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
    

    lv_menu_set_page(g_gps_state.menu, g_gps_state.page);

    GPSData gps_data = getGPSData();
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
        
        g_gps_state.lat = DEFAULT_LAT;
        g_gps_state.lng = DEFAULT_LNG;
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
    
    
    if (g_gps_state.menu) {
        lv_obj_clean(g_gps_state.menu);
        lv_obj_del(g_gps_state.menu);
    }
    
    
    GPSPageState empty = {};
    g_gps_state = empty;
    
    
    reset_title_status_cache();
    
    
    extern lv_group_t *app_g;
    bind_encoder_to_group(app_g);
    
    GPS_LOG("[GPS] GPS page exited, all state cleared\n");
}
