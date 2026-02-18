#include "../app/app_context.h"
#include "board/BoardBase.h"
#include "display/DisplayInterface.h"
#include "gps/GPS.h"
#include "screens/gps/gps_constants.h"
#include "screens/gps/gps_modal.h"
#include "screens/gps/gps_page_components.h"
#include "screens/gps/gps_page_input.h"
#include "screens/gps/gps_page_layout.h"
#include "screens/gps/gps_page_lifetime.h"
#include "screens/gps/gps_page_map.h"
#include "screens/gps/gps_page_styles.h"
#include "screens/gps/gps_route_overlay.h"
#include "screens/gps/gps_state.h"
#include "screens/gps/gps_tracker_overlay.h"
#include "ui_common.h"
#include "widgets/map/map_tiles.h"

#include <Arduino.h>
#include <cmath>

#include "../gps/gps_service_api.h"

using GPSData = gps::GpsState;
extern void updateUserActivity();

#define GPS_DEBUG 0
#ifndef GPS_LOG
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif
#endif

void ui_gps_exit(lv_obj_t* parent);

static void gps_top_bar_back(void* /*user_data*/)
{
    GPS_LOG("[GPS][BACK] gps_top_bar_back: exiting=%d alive=%d root=%p\n",
            g_gps_state.exiting, g_gps_state.alive, gps_root);
    if (g_gps_state.exiting)
    {
        GPS_LOG("[GPS][BACK] gps_top_bar_back: already exiting, ignore\n");
        return;
    }
    g_gps_state.exiting = true;
    GPS_LOG("[GPS][BACK] gps_top_bar_back: scheduling async exit\n");
    ui_request_exit_to_menu();
}

GPSPageState g_gps_state;
static lv_obj_t* gps_root = nullptr;

namespace
{

void assign_layout_widgets(const gps::ui::layout::Widgets& w)
{
    gps_root = w.root;
    g_gps_state.root = w.root;
    g_gps_state.header = w.header;
    g_gps_state.page = w.content;
    g_gps_state.map = w.map;
    g_gps_state.resolution_label = w.resolution_label;
    g_gps_state.altitude_label = w.altitude_label;
    g_gps_state.panel = w.panel;
    g_gps_state.member_panel = w.member_panel;
    g_gps_state.zoom = w.zoom_btn;
    g_gps_state.pos = w.pos_btn;
    g_gps_state.pan_h = w.pan_h_btn;
    g_gps_state.pan_v = w.pan_v_btn;
    g_gps_state.tracker_btn = w.tracker_btn;
    g_gps_state.layer_btn = w.layer_btn;
    g_gps_state.route_btn = w.route_btn;
    g_gps_state.top_bar = w.top_bar;
}

void bind_controls_and_group(lv_group_t* app_g)
{
    set_control_id(g_gps_state.top_bar.back_btn, ControlId::BackBtn);
    set_control_id(g_gps_state.map, ControlId::Map);
    set_control_id(g_gps_state.zoom, ControlId::ZoomBtn);
    set_control_id(g_gps_state.pos, ControlId::PosBtn);
    set_control_id(g_gps_state.pan_h, ControlId::PanHBtn);
    set_control_id(g_gps_state.pan_v, ControlId::PanVBtn);
    set_control_id(g_gps_state.tracker_btn, ControlId::TrackerBtn);
    set_control_id(g_gps_state.layer_btn, ControlId::LayerBtn);
    set_control_id(g_gps_state.route_btn, ControlId::RouteBtn);

    auto bind_btn_events = [](lv_obj_t* obj, bool include_rotary)
    {
        if (!obj) return;
        lv_obj_add_event_cb(obj, on_ui_event, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(obj, on_ui_event, LV_EVENT_KEY, NULL);
        if (include_rotary)
        {
            lv_obj_add_event_cb(obj, on_ui_event, LV_EVENT_ROTARY, NULL);
        }
    };

    bind_btn_events(g_gps_state.zoom, true);
    bind_btn_events(g_gps_state.pos, true);
    bind_btn_events(g_gps_state.pan_h, true);
    bind_btn_events(g_gps_state.pan_v, true);
    bind_btn_events(g_gps_state.tracker_btn, false);
    bind_btn_events(g_gps_state.layer_btn, false);
    bind_btn_events(g_gps_state.route_btn, false);

    if (g_gps_state.top_bar.back_btn)
    {
        // Ensure encoder KEY events can trigger back even if LVGL doesn't emit CLICKED.
        lv_obj_add_event_cb(g_gps_state.top_bar.back_btn, on_ui_event, LV_EVENT_KEY, NULL);
    }

    if (app_g)
    {
        if (g_gps_state.top_bar.back_btn)
        {
            lv_group_add_obj(app_g, g_gps_state.top_bar.back_btn);
        }
        lv_group_add_obj(app_g, g_gps_state.zoom);
        lv_group_add_obj(app_g, g_gps_state.pos);
        lv_group_add_obj(app_g, g_gps_state.pan_h);
        lv_group_add_obj(app_g, g_gps_state.pan_v);
        lv_group_add_obj(app_g, g_gps_state.tracker_btn);
        lv_group_add_obj(app_g, g_gps_state.layer_btn);
        lv_group_add_obj(app_g, g_gps_state.route_btn);
    }

    lv_obj_add_event_cb(g_gps_state.map, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.map, on_ui_event, LV_EVENT_ROTARY, NULL);
}

void init_gps_state_defaults()
{
    g_gps_state.exiting = false;
    GPSData gps_data = gps::gps_get_data();
    constexpr double kCoordEps = 1e-6;
    const bool has_cached_coord = std::isfinite(gps_data.lat) &&
                                  std::isfinite(gps_data.lng) &&
                                  (std::fabs(gps_data.lat) > kCoordEps || std::fabs(gps_data.lng) > kCoordEps);

    if (gps_data.valid)
    {
        g_gps_state.lat = gps_data.lat;
        g_gps_state.lng = gps_data.lng;
        g_gps_state.has_fix = true;
    }
    else
    {
        g_gps_state.zoom_level = gps_ui::kDefaultZoom;
        if (has_cached_coord)
        {
            // Keep last known location so offline maps remain useful before reacquiring fix.
            g_gps_state.lat = gps_data.lat;
            g_gps_state.lng = gps_data.lng;
        }
        else
        {
            g_gps_state.lat = gps_ui::kDefaultLat;
            g_gps_state.lng = gps_ui::kDefaultLng;
        }
        g_gps_state.has_fix = false;
    }

    g_gps_state.has_map_data = false;
    g_gps_state.has_visible_map_data = false;

    g_gps_state.pan_x = 0;
    g_gps_state.pan_y = 0;
    g_gps_state.follow_position = true;

    g_gps_state.pan_h_editing = false;
    g_gps_state.pan_v_editing = false;
    g_gps_state.edit_mode = 0;

    g_gps_state.pending_refresh = false;
    g_gps_state.last_resolution_lat = 0.0;
    g_gps_state.last_resolution_zoom = -1;

    g_gps_state.anchor.valid = false;
    g_gps_state.initial_load_ms = 0;
    g_gps_state.initial_tiles_loaded = false;

    g_gps_state.tiles.clear();
    g_gps_state.tiles.reserve(TILE_RECORD_LIMIT);
}

} // namespace

bool isGPSLoadingTiles()
{
    return false;
}

static void title_update_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!gps::ui::lifetime::is_alive())
    {
        return;
    }

    reset_title_status_cache();
    update_title_and_status();
}

static void gps_update_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!gps::ui::lifetime::is_alive())
    {
        return;
    }

    // If there's no GPS fix and nothing explicitly requested a refresh,
    // avoid driving full UI/map updates on every tick.
    GPSData gps_data = gps::gps_get_data();
    const bool has_fix_now = gps_data.valid || g_gps_state.has_fix;

    if (g_gps_state.pending_refresh)
    {
        g_gps_state.pending_refresh = false;
        update_map_tiles(false);
    }

    refresh_member_panel(false);
    refresh_team_markers_from_posring();
    refresh_team_signal_markers_from_chatlog();

    if (g_gps_state.zoom_modal.is_open())
    {
        tick_gps_update(false);
        return;
    }

    if (modal_is_open(g_gps_state.tracker_modal))
    {
        tick_gps_update(false);
        return;
    }
    if (modal_is_open(g_gps_state.layer_modal))
    {
        tick_gps_update(false);
        return;
    }

    if (!has_fix_now && !g_gps_state.pending_refresh)
    {
        return;
    }

    tick_gps_update(true);
}

static void gps_loader_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!gps::ui::lifetime::is_alive())
    {
        return;
    }
    tick_loader();
}

static void gps_initial_tiles_async(void* /*user_data*/)
{
    if (!gps::ui::lifetime::is_alive() || !g_gps_state.map)
    {
        return;
    }
    // Ensure final sizes before first tile calculation to avoid visible jitter.
    lv_obj_update_layout(gps_root);
    update_map_tiles(false);
}

void ui_gps_enter(lv_obj_t* parent)
{
    GPS_LOG("[GPS] Entering GPS page, SD ready: %d, GPS ready: %d\n", board.isSDReady(), board.isGPSReady());

    // Ensure any previous instance is cleaned up before we reset state.
    if (gps_root != nullptr)
    {
        ui_gps_exit(nullptr);
    }

    reset_control_tags();
    g_gps_state = GPSPageState{};

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    extern lv_group_t* app_g;

    gps::ui::layout::Spec spec{};
    gps::ui::layout::Widgets w{};
    gps::ui::layout::create(parent, spec, w);
    gps::ui::styles::apply_all(w, spec);

    assign_layout_widgets(w);

    gps::ui::lifetime::mark_alive(gps_root, app_g);
    gps::ui::lifetime::bind_root_delete_hook();

    // Initialize TopBar on the state-owned instance (user_data must outlive layout locals).
    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(g_gps_state.top_bar, g_gps_state.header, cfg);
    ::ui::widgets::top_bar_set_title(g_gps_state.top_bar, "Map");
    ::ui::widgets::top_bar_set_back_callback(g_gps_state.top_bar, gps_top_bar_back, nullptr);

    // Ensure layout sizes are finalized before any tile calculations.
    lv_obj_update_layout(gps_root);

    ::init_tile_context(g_gps_state.tile_ctx,
                        nullptr,
                        &g_gps_state.anchor,
                        &g_gps_state.tiles,
                        &g_gps_state.has_map_data,
                        &g_gps_state.has_visible_map_data);
    g_gps_state.tile_ctx.map_container = g_gps_state.map;

    if (!g_gps_state.tracker_draw_cb_bound)
    {
        lv_obj_add_event_cb(g_gps_state.map, gps_tracker_draw_event, LV_EVENT_DRAW_POST, NULL);
        g_gps_state.tracker_draw_cb_bound = true;
    }
    if (!g_gps_state.route_draw_cb_bound)
    {
        lv_obj_add_event_cb(g_gps_state.map, gps_route_draw_event, LV_EVENT_DRAW_POST, NULL);
        g_gps_state.route_draw_cb_bound = true;
    }

    lv_label_set_text(g_gps_state.resolution_label, "");
    if (g_gps_state.altitude_label)
    {
        lv_label_set_text(g_gps_state.altitude_label, "Alt: -- m");
    }

    bind_controls_and_group(app_g);

    lv_obj_move_foreground(g_gps_state.panel);
    if (g_gps_state.resolution_label != NULL)
    {
        lv_obj_move_foreground(g_gps_state.resolution_label);
    }
    if (g_gps_state.altitude_label != NULL)
    {
        lv_obj_move_foreground(g_gps_state.altitude_label);
    }

    init_gps_state_defaults();

    hide_pan_h_indicator();
    hide_pan_v_indicator();

    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        const auto& cfg = app_ctx.getConfig();
        bool show_route = cfg.route_enabled && (cfg.route_path[0] != '\0');
        if (g_gps_state.route_btn)
        {
            if (show_route)
            {
                lv_obj_clear_flag(g_gps_state.route_btn, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(g_gps_state.route_btn, LV_OBJ_FLAG_HIDDEN);
                if (app_g)
                {
                    lv_group_remove_obj(g_gps_state.route_btn);
                }
            }
        }
        if (show_route)
        {
            gps_route_sync_from_config(false);
        }
    }

    refresh_member_panel(true);

    if (app_g != NULL)
    {
        lv_group_set_editing(app_g, false);
    }

    reset_title_status_cache();
    update_zoom_btn();

    g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
    g_gps_state.last_resolution_lat = g_gps_state.lat;
    update_resolution_display();
    update_altitude_display(gps::gps_get_data());

    if (g_gps_state.map == NULL || g_gps_state.tile_ctx.map_container != g_gps_state.map)
    {
        GPS_LOG("[GPS] WARNING: map=%p, tile_ctx.map_container=%p, skipping initial tile calculation\n",
                g_gps_state.map, g_gps_state.tile_ctx.map_container);
    }
    else
    {
        // Defer the first tile calculation to the next LVGL tick to stabilize layout.
        lv_async_call(gps_initial_tiles_async, nullptr);
    }

    if (app_g)
    {
        set_default_group(app_g);
        lv_group_set_editing(app_g, false);
    }
    else
    {
        set_default_group(prev_group);
    }

    // Split timers: fast tile loading + slower GPS refresh.
    g_gps_state.loader_timer = gps::ui::lifetime::add_timer(gps_loader_timer_cb, 200, NULL);
    g_gps_state.timer = gps::ui::lifetime::add_timer(gps_update_timer_cb, 500, NULL);
    g_gps_state.title_timer = gps::ui::lifetime::add_timer(title_update_timer_cb, 30000, NULL);

    update_title_and_status();

    GPS_LOG("[GPS] GPS page initialized: main_timer=%p, title_timer=%p\n", g_gps_state.timer, g_gps_state.title_timer);
}

void ui_gps_exit(lv_obj_t* parent)
{
    (void)parent;

    GPS_LOG("[GPS] Exiting GPS page\n");
    GPS_LOG("[GPS][EXIT] begin: alive=%d exiting=%d root=%p\n",
            g_gps_state.alive, g_gps_state.exiting, gps_root);

    // Prevent re-entrant exit.
    g_gps_state.exiting = true;

    // Mirror Contacts cleanup order: stop timers/inputs, close modals, delete root, reset state.
    gps::ui::lifetime::clear_timers();
    g_gps_state.timer = nullptr;
    g_gps_state.loader_timer = nullptr;
    g_gps_state.title_timer = nullptr;
    g_gps_state.toast_timer = nullptr;
    GPS_LOG("[GPS][EXIT] timers cleared\n");

    extern lv_group_t* app_g;
    if (app_g)
    {
        auto remove_if = [](lv_obj_t* obj)
        {
            if (obj)
            {
                lv_group_remove_obj(obj);
            }
        };
        remove_if(g_gps_state.top_bar.back_btn);
        remove_if(g_gps_state.zoom);
        remove_if(g_gps_state.pos);
        remove_if(g_gps_state.pan_h);
        remove_if(g_gps_state.pan_v);
        remove_if(g_gps_state.tracker_btn);
        remove_if(g_gps_state.layer_btn);
        remove_if(g_gps_state.route_btn);
        remove_if(g_gps_state.pan_h_indicator);
        remove_if(g_gps_state.pan_v_indicator);
        for (auto* btn : g_gps_state.member_btns)
        {
            remove_if(btn);
        }
        GPS_LOG("[GPS][EXIT] removed objs from group\n");
    }

    if (g_gps_state.zoom_modal.is_open())
    {
        GPS_LOG("[GPS][EXIT] closing zoom modal\n");
        modal_close(g_gps_state.zoom_modal);
    }
    if (g_gps_state.layer_modal.is_open())
    {
        GPS_LOG("[GPS][EXIT] closing layer modal\n");
        modal_close(g_gps_state.layer_modal);
    }
    GPS_LOG("[GPS][EXIT] cleaning tracker overlay\n");
    gps_tracker_cleanup();
    gps_route_cleanup();
    if (g_gps_state.zoom_modal.group)
    {
        GPS_LOG("[GPS][EXIT] deleting zoom modal group\n");
        lv_group_del(g_gps_state.zoom_modal.group);
        g_gps_state.zoom_modal.group = nullptr;
    }
    if (g_gps_state.tracker_modal.group)
    {
        GPS_LOG("[GPS][EXIT] deleting tracker modal group\n");
        lv_group_del(g_gps_state.tracker_modal.group);
        g_gps_state.tracker_modal.group = nullptr;
    }
    if (g_gps_state.layer_modal.group)
    {
        GPS_LOG("[GPS][EXIT] deleting layer modal group\n");
        lv_group_del(g_gps_state.layer_modal.group);
        g_gps_state.layer_modal.group = nullptr;
    }

    GPS_LOG("[GPS][EXIT] cleanup tiles\n");
    ::cleanup_tiles(g_gps_state.tile_ctx);

    // Delete the root last.
    if (gps_root != nullptr)
    {
        GPS_LOG("[GPS][EXIT] deleting root %p\n", gps_root);
        lv_obj_del(gps_root);
        gps_root = nullptr;
    }

    reset_title_status_cache();

    // Do not rebind encoder here; menu_show() already sets the default/menu group.

    g_gps_state = GPSPageState{};
    GPS_LOG("[GPS][EXIT] end: state reset, root=%p\n", gps_root);
}
