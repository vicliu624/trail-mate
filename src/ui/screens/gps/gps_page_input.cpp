#include "gps_page_input.h"
#include "../../ui_common.h"
#include "../../widgets/map/map_tiles.h"
#include "gps_constants.h"
#include "gps_modal.h"
#include "gps_page_lifetime.h"
#include "gps_route_overlay.h"
#include "gps_state.h"
#include "gps_tracker_overlay.h"
#include <Arduino.h>
#include <cstdint>
#include <cstdio>

#define GPS_DEBUG 1 // Enable debug logging
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

extern GPSPageState g_gps_state;
extern void updateUserActivity();

using gps::ui::lifetime::is_alive;

// GPS data access - now provided by TLoRaPagerBoard
#include "../../gps/gps_service_api.h"

using GPSData = gps::GpsState;

static bool indev_is_pressed(lv_event_t* e)
{
    lv_indev_t* indev = lv_event_get_indev(e);
    if (!indev) return false;
    return lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;
}

ControlId ctrl_id(lv_obj_t* obj)
{
    if (obj == nullptr) return ControlId::Page;
    auto* tag = (ControlTag*)lv_obj_get_user_data(obj);
    return tag ? tag->id : ControlId::Page;
}

// Control tag pool (fixed-size, reset on enter).
static ControlTag s_tags[256];
static uint16_t s_tag_index = 0;

void reset_control_tags()
{
    s_tag_index = 0;
    GPS_LOG("[GPS] reset_control_tags: tag index reset to 0\n");
}

void set_control_id(lv_obj_t* obj, ControlId id)
{
    if (obj == nullptr) return;
    if (s_tag_index < (sizeof(s_tags) / sizeof(s_tags[0])))
    {
        s_tags[s_tag_index].id = id;
        lv_obj_set_user_data(obj, &s_tags[s_tag_index]);
        s_tag_index++;
    }
    else
    {
        GPS_LOG("[GPS] WARNING: ControlTag pool exhausted! tag_index=%d\n", s_tag_index);
    }
}

static void handle_click(lv_obj_t* target);
static void handle_rotary(lv_obj_t* target, int32_t diff);
static void handle_key(lv_obj_t* target, lv_key_t key, lv_event_t* e);

static void action_back_exit();
static void action_back_exit_async(void* user_data);
static void action_zoom_open_popup();
static void action_position_center();
static void action_pan_enter(ControlId axis_id);
static void action_pan_exit();
static void action_pan_step(ControlId axis_id, int32_t step);
static void action_route_focus();

void zoom_popup_handle_rotary(int32_t diff);
void zoom_popup_handle_key(lv_key_t key, lv_event_t* e);

static void exit_pan_h_mode()
{
    extern void hide_pan_h_indicator();
    g_gps_state.pan_h_editing = false;
    g_gps_state.edit_mode = 0;
    hide_pan_h_indicator();

    extern lv_group_t* app_g;
    if (app_g != NULL)
    {
        lv_group_set_editing(app_g, false);
        if (g_gps_state.pan_h != NULL)
        {
            lv_group_focus_obj(g_gps_state.pan_h);
        }
    }
    GPS_LOG("[GPS] exit_pan_h_mode: exited horizontal pan editing mode\n");
}

static void exit_pan_v_mode()
{
    extern void hide_pan_v_indicator();
    g_gps_state.pan_v_editing = false;
    g_gps_state.edit_mode = 0;
    hide_pan_v_indicator();

    extern lv_group_t* app_g;
    if (app_g != NULL)
    {
        lv_group_set_editing(app_g, false);
        if (g_gps_state.pan_v != NULL)
        {
            lv_group_focus_obj(g_gps_state.pan_v);
        }
    }
    GPS_LOG("[GPS] exit_pan_v_mode: exited vertical pan editing mode\n");
}

// 注意：边沿检测已移到 poll_encoder_for_pan() 中，这里不再需�?
void on_ui_event(lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    auto code = lv_event_get_code(e);
    auto* target = (lv_obj_t*)lv_event_get_target(e);

    // ============================================================================
    // DEBUG: 详细日志，抓取真实事件类型和参数
    // ============================================================================
    extern lv_group_t* app_g;
    lv_obj_t* focused = app_g ? lv_group_get_focused(app_g) : nullptr;
    bool editing = app_g ? lv_group_get_editing(app_g) : false;
    ControlId target_id = ctrl_id(target);
    if (target_id == ControlId::BackBtn)
    {
        GPS_LOG("[GPS][BACK] on_ui_event: code=%d target=%p focused=%p editing=%d\n", (int)code, target, focused, editing);
    }

    if (code == LV_EVENT_KEY)
    {
        lv_key_t key = (lv_key_t)lv_event_get_key(e);
        GPS_LOG("[GPS] EVENT: KEY, key=%d, target=%p(id=%d), focused=%p, editing=%d, pan_h=%d, pan_v=%d\n",
                key, target, (int)target_id, focused, editing, g_gps_state.pan_h_editing, g_gps_state.pan_v_editing);
    }
    else if (code == LV_EVENT_ROTARY)
    {
        int32_t diff = lv_event_get_rotary_diff(e);
        GPS_LOG("[GPS] EVENT: ROTARY, diff=%d, target=%p(id=%d), focused=%p, editing=%d, pan_h=%d, pan_v=%d\n",
                diff, target, (int)target_id, focused, editing, g_gps_state.pan_h_editing, g_gps_state.pan_v_editing);
    }
    else if (code == LV_EVENT_CLICKED)
    {
        GPS_LOG("[GPS] EVENT: CLICKED, target=%p(id=%d), focused=%p, editing=%d, pan_h=%d, pan_v=%d\n",
                target, (int)target_id, focused, editing, g_gps_state.pan_h_editing, g_gps_state.pan_v_editing);
    }
    else if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED)
    {
        GPS_LOG("[GPS] EVENT: %s, target=%p(id=%d), focused=%p, editing=%d, pan_h=%d, pan_v=%d\n",
                code == LV_EVENT_PRESSED ? "PRESSED" : "RELEASED",
                target, (int)target_id, focused, editing, g_gps_state.pan_h_editing, g_gps_state.pan_v_editing);
    }

    // CRITICAL: Handle indicator events FIRST, matching original unified_button_handler logic
    // In original code, indicator events were checked before any other processing
    // Use pan_h_editing/pan_v_editing to match original code exactly
    if (target == g_gps_state.pan_h_indicator)
    {
        if (g_gps_state.zoom_modal.is_open()) return;
        updateUserActivity();




        // Key event: encoder rotation generates KEY events.
        if (code == LV_EVENT_KEY && g_gps_state.pan_h_editing)
        {
            lv_key_t key = (lv_key_t)lv_event_get_key(e);

            int32_t step = 0;
            if (key == ENCODER_KEY_ROTATE_DOWN)
            {
                step = +1;
            }
            else if (key == ENCODER_KEY_ROTATE_UP)
            {
                step = -1;
            }
            else
            {
                GPS_LOG("[GPS] PanH KEY: unexpected keycode=%d (expected %d or %d), ignoring\n",
                        key, ENCODER_KEY_ROTATE_UP, ENCODER_KEY_ROTATE_DOWN);
                return;
            }

            g_gps_state.pan_x += step * gps_ui::kMapPanStep;
            g_gps_state.pending_refresh = true;
            if (g_gps_state.map != NULL)
            {
                lv_obj_invalidate(g_gps_state.map);
            }
            return;
        }

        // CLICKED is handled by pan_indicator_event_cb.
        return;
    }

    if (target == g_gps_state.pan_v_indicator)
    {
        if (g_gps_state.zoom_modal.is_open()) return;
        updateUserActivity();

        // Key event: encoder rotation generates KEY events.
        if (code == LV_EVENT_KEY && g_gps_state.pan_v_editing)
        {
            lv_key_t key = (lv_key_t)lv_event_get_key(e);

            int32_t step = 0;
            if (key == ENCODER_KEY_ROTATE_DOWN)
            {
                step = +1;
            }
            else if (key == ENCODER_KEY_ROTATE_UP)
            {
                step = -1;
            }
            else
            {
                GPS_LOG("[GPS] PanV KEY: unexpected keycode=%d (expected %d or %d), ignoring\n",
                        key, ENCODER_KEY_ROTATE_UP, ENCODER_KEY_ROTATE_DOWN);
                return;
            }

            GPS_LOG("[GPS] PanV KEY: key=%d, step=%d, pan_y: %d -> %d\n",
                    key, step, g_gps_state.pan_y, g_gps_state.pan_y + step * gps_ui::kMapPanStep);
            g_gps_state.pan_y += step * gps_ui::kMapPanStep;
            g_gps_state.pending_refresh = true;
            if (g_gps_state.map != NULL)
            {
                lv_obj_invalidate(g_gps_state.map);
            }
            return;
        }

        // CLICKED is handled by pan_indicator_event_cb.
        return;
    }

    const bool is_back_btn = (target_id == ControlId::BackBtn);

    if (g_gps_state.zoom_modal.is_open())
    {
        if (!is_back_btn &&
            target_id != ControlId::ZoomValueLabel &&
            target_id != ControlId::ZoomWin)
        {
            return;
        }
    }

    if (modal_is_open(g_gps_state.tracker_modal) && !is_back_btn)
    {
        return;
    }

    switch (code)
    {
    case LV_EVENT_CLICKED:
        handle_click(target);
        break;
    // LV_EVENT_ROTARY 从未收到，实际收到的�?LV_EVENT_KEY (keycode 19/20)
    // case LV_EVENT_ROTARY:
    //     handle_rotary(target, lv_event_get_rotary_diff(e));
    //     break;
    case LV_EVENT_KEY:
        handle_key(target, (lv_key_t)lv_event_get_key(e), e);
        break;
    default:
        break;
    }
}

void pan_indicator_event_cb(lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);

    GPS_LOG("[GPS] pan_indicator_event_cb: code=%d, target=%p, pan_h_indicator=%p, pan_v_indicator=%p\n",
            code, target, g_gps_state.pan_h_indicator, g_gps_state.pan_v_indicator);

    if (code == LV_EVENT_CLICKED)
    {
        GPS_LOG("[GPS] pan_indicator_event_cb: CLICKED event received\n");
        if (target == g_gps_state.pan_h_indicator)
        {
            GPS_LOG("[GPS] pan_indicator_event_cb: target is pan_h_indicator, exiting editing mode\n");
            g_gps_state.edit_mode = 0;         // Exit PanH editing mode
            g_gps_state.pan_h_editing = false; // Keep in sync with original code
            extern void hide_pan_h_indicator();
            hide_pan_h_indicator();
            extern lv_group_t* app_g;
            if (app_g != NULL)
            {
                lv_group_set_editing(app_g, false);
                if (g_gps_state.pan_h != NULL)
                {
                    lv_group_focus_obj(g_gps_state.pan_h);
                }
            }
            GPS_LOG("[GPS] Horizontal pan indicator: CLICKED, exiting editing mode\n");
        }
        else if (target == g_gps_state.pan_v_indicator)
        {
            GPS_LOG("[GPS] pan_indicator_event_cb: target is pan_v_indicator, exiting editing mode\n");
            g_gps_state.edit_mode = 0;         // Exit PanV editing mode
            g_gps_state.pan_v_editing = false; // Keep in sync with original code
            extern void hide_pan_v_indicator();
            hide_pan_v_indicator();
            extern lv_group_t* app_g;
            if (app_g != NULL)
            {
                lv_group_set_editing(app_g, false);
                if (g_gps_state.pan_v != NULL)
                {
                    lv_group_focus_obj(g_gps_state.pan_v);
                }
            }
            GPS_LOG("[GPS] Vertical pan indicator: CLICKED, exiting editing mode\n");
        }
        else
        {
            GPS_LOG("[GPS] pan_indicator_event_cb: target mismatch! target=%p\n", target);
        }
    }
}

static void handle_click(lv_obj_t* target)
{
    ControlId id = ctrl_id(target);
    updateUserActivity();

    GPS_LOG("[GPS] handle_click: id=%d, edit_mode=%d\n", (int)id, g_gps_state.edit_mode);

    switch (id)
    {
    case ControlId::BackBtn:
        action_back_exit();
        break;
    case ControlId::ZoomBtn:
        action_zoom_open_popup();
        break;

    case ControlId::PosBtn:
        action_position_center();
        break;

    case ControlId::PanHBtn:
        if (g_gps_state.pan_h_editing)
        { // Use pan_h_editing to match original
            action_pan_exit();
        }
        else
        {
            action_pan_enter(ControlId::PanHBtn);
        }
        break;

    case ControlId::PanVBtn:
        if (g_gps_state.pan_v_editing)
        { // Use pan_v_editing to match original
            action_pan_exit();
        }
        else
        {
            action_pan_enter(ControlId::PanVBtn);
        }
        break;

    case ControlId::TrackerBtn:
        gps_tracker_open_modal();
        break;

    case ControlId::RouteBtn:
        action_route_focus();
        break;
    default:
        break;
    }
}

static void handle_rotary(lv_obj_t* target, int32_t diff)
{
    if (diff == 0) return;

    ControlId id = ctrl_id(target);
    updateUserActivity();

    GPS_LOG("[GPS] handle_rotary: target_id=%d, edit_mode=%d, diff=%d\n",
            (int)id, g_gps_state.edit_mode, diff);

    if (g_gps_state.zoom_modal.is_open())
    {
        if (id == ControlId::ZoomValueLabel || id == ControlId::ZoomWin)
        {
            extern void zoom_popup_handle_rotary(int32_t diff);
            zoom_popup_handle_rotary(diff);
            return;
        }
    }

    // When in pan editing mode, handle rotary regardless of target
    // This allows rotary to work even if focus is on a different object
    // Use pan_h_editing/pan_v_editing to match original code
    if (g_gps_state.pan_h_editing)
    {
        GPS_LOG("[GPS] handle_rotary: PanH editing mode, calling action_pan_step\n");
        action_pan_step(ControlId::PanHIndicator, diff);
        return;
    }

    if (g_gps_state.pan_v_editing)
    {
        GPS_LOG("[GPS] handle_rotary: PanV editing mode, calling action_pan_step\n");
        action_pan_step(ControlId::PanVIndicator, diff);
        return;
    }
}

static void handle_key(lv_obj_t* target, lv_key_t key, lv_event_t* e)
{
    ControlId id = ctrl_id(target);
    updateUserActivity();

    if (key == LV_KEY_BACKSPACE)
    {
        action_back_exit();
        return;
    }

    if (id == ControlId::BackBtn)
    {
        if (key == LV_KEY_ENTER || key == LV_KEY_ESC)
        {
            action_back_exit();
            return;
        }
    }

    if (g_gps_state.zoom_modal.is_open())
    {
        if (id == ControlId::ZoomValueLabel || id == ControlId::ZoomWin)
        {
            extern void zoom_popup_handle_key(lv_key_t key, lv_event_t * e);
            zoom_popup_handle_key(key, e);
            return;
        }
    }

    if (modal_is_open(g_gps_state.tracker_modal))
    {
        return;
    }

    if (key == LV_KEY_ENTER)
    {
        if (id == ControlId::BackBtn)
        {
            action_back_exit();
            return;
        }
        if (id == ControlId::ZoomBtn)
        {
            action_zoom_open_popup();
            return;
        }
        if (id == ControlId::PosBtn)
        {
            action_position_center();
            return;
        }
        if (id == ControlId::PanHBtn)
        {
            if (g_gps_state.pan_h_editing)
            { // Use pan_h_editing to match original
                action_pan_exit();
            }
            else
            {
                action_pan_enter(ControlId::PanHBtn);
            }
            return;
        }
        if (id == ControlId::PanVBtn)
        {
            if (g_gps_state.pan_v_editing)
            { // Use pan_v_editing to match original
                action_pan_exit();
            }
            else
            {
                action_pan_enter(ControlId::PanVBtn);
            }
            return;
        }
        if (id == ControlId::TrackerBtn)
        {
            gps_tracker_open_modal();
            return;
        }
        if (id == ControlId::RouteBtn)
        {
            action_route_focus();
            return;
        }
    }

    if (key == 'z' || key == 'Z')
    {
        action_zoom_open_popup();
        return;
    }

    if (key == 'p' || key == 'P')
    {
        action_position_center();
        return;
    }

    if (key == 'h' || key == 'H')
    {
        if (g_gps_state.pan_h_editing)
        { // Use pan_h_editing to match original
            action_pan_exit();
        }
        else
        {
            action_pan_enter(ControlId::PanHBtn);
        }
        return;
    }

    if (key == 'v' || key == 'V')
    {
        if (g_gps_state.pan_v_editing)
        { // Use pan_v_editing to match original
            action_pan_exit();
        }
        else
        {
            action_pan_enter(ControlId::PanVBtn);
        }
        return;
    }

    if (key == 't' || key == 'T')
    {
        gps_tracker_open_modal();
        return;
    }

    if (key == 'r' || key == 'R')
    {
        action_route_focus();
        return;
    }
}

static void action_back_exit()
{
    if (!is_alive())
    {
        return;
    }
    if (g_gps_state.exiting)
    {
        return;
    }
    g_gps_state.exiting = true;
    // Exiting the screen can delete the current event target.
    // Schedule it asynchronously to avoid deleting during an LVGL callback.
    GPS_LOG("[GPS][BACK] action_back_exit: scheduling async exit (alive=%d exiting=%d root=%p)\n", g_gps_state.alive, g_gps_state.exiting, g_gps_state.root);
    lv_async_call(action_back_exit_async, nullptr);
}

static void action_back_exit_async(void* /*user_data*/)
{
    GPS_LOG("[GPS][BACK] action_back_exit_async: requesting exit to menu (alive=%d exiting=%d root=%p)\n",
            g_gps_state.alive, g_gps_state.exiting, g_gps_state.root);
    ui_request_exit_to_menu();
}

static void action_zoom_open_popup()
{
    if (!is_alive())
    {
        return;
    }
    extern void show_zoom_popup();
    uint32_t now = millis();
    if (g_gps_state.zoom_modal.close_ms > 0 &&
        (now - g_gps_state.zoom_modal.close_ms) < 300)
    {
        return;
    }

    if (!g_gps_state.zoom_modal.is_open())
    {
        show_zoom_popup();
    }
}

static void action_position_center()
{
    if (!is_alive())
    {
        return;
    }
    extern void show_toast(const char* message, uint32_t duration_ms);
    extern void update_map_tiles(bool lightweight);
    extern void create_gps_marker();

    // Get latest GPS data
    GPSData gps_data = gps::gps_get_data();

    if (!gps_data.valid)
    {
        show_toast("No GPS data", 2000);
        GPS_LOG("[GPS] Position action: No GPS data, showing toast\n");
        return;
    }

    // Update to latest GPS coordinates
    g_gps_state.lat = gps_data.lat;
    g_gps_state.lng = gps_data.lng;
    g_gps_state.has_fix = true;

    // Reset pan to center the GPS position
    g_gps_state.pan_x = 0;
    g_gps_state.pan_y = 0;

    // Create or update GPS marker
    create_gps_marker();

    // Update map to show GPS position centered
    update_map_tiles(false);
    GPS_LOG("[GPS] Position action: centered GPS marker at lat=%.6f, lng=%.6f\n",
            g_gps_state.lat, g_gps_state.lng);
}

static void action_route_focus()
{
    if (!is_alive())
    {
        return;
    }
    extern void show_toast(const char* message, uint32_t duration_ms);
    if (!gps_route_focus(true))
    {
        show_toast("No route", 1500);
    }
}

static void action_pan_enter(ControlId axis_id)
{
    if (!is_alive())
    {
        return;
    }
    extern void show_pan_h_indicator();
    extern void show_pan_v_indicator();
    extern void hide_pan_h_indicator();
    extern void hide_pan_v_indicator();

    if (axis_id == ControlId::PanHBtn)
    {
        g_gps_state.edit_mode = 1;         // PanH
        g_gps_state.pan_h_editing = true;  // Keep in sync with original code
        g_gps_state.pan_v_editing = false; // Keep in sync with original code
        hide_pan_v_indicator();
        show_pan_h_indicator();

        extern lv_group_t* app_g;
        if (app_g != NULL && g_gps_state.pan_h_indicator != NULL)
        {
            set_default_group(app_g);
            bind_encoder_to_group(app_g);

            // CRITICAL: 确保 indicator �?group 中且可聚�?            // 如果 focus 失败，说�?indicator 没在 group 里或不可聚焦
            lv_group_focus_obj(g_gps_state.pan_h_indicator);
            lv_group_set_editing(app_g, true);

            // Focus diagnostics.
            lv_obj_t* f = lv_group_get_focused(app_g);
            bool editing = lv_group_get_editing(app_g);
            GPS_LOG("[GPS] Horizontal pan: editing mode ON, focus=%p, indicator=%p, editing=%d\n",
                    f, g_gps_state.pan_h_indicator, editing);

            if (f != g_gps_state.pan_h_indicator)
            {
                GPS_LOG("[GPS] ERROR: Focus mismatch! focus=%p, indicator=%p - indicator may not be in group or not focusable\n",
                        f, g_gps_state.pan_h_indicator);
            }
        }
    }
    else if (axis_id == ControlId::PanVBtn)
    {
        g_gps_state.edit_mode = 2;         // PanV
        g_gps_state.pan_v_editing = true;  // Keep in sync with original code
        g_gps_state.pan_h_editing = false; // Keep in sync with original code
        hide_pan_h_indicator();
        show_pan_v_indicator();

        extern lv_group_t* app_g;
        if (app_g != NULL && g_gps_state.pan_v_indicator != NULL)
        {
            set_default_group(app_g);
            bind_encoder_to_group(app_g);
            lv_group_focus_obj(g_gps_state.pan_v_indicator);
            lv_group_set_editing(app_g, true);

            // Focus diagnostics.
            lv_obj_t* f = lv_group_get_focused(app_g);
            GPS_LOG("[GPS] Vertical pan: editing mode ON, focus=%p, indicator=%p, editing=%d\n",
                    f, g_gps_state.pan_v_indicator, lv_group_get_editing(app_g));
        }
    }
}

static void action_pan_exit()
{
    if (g_gps_state.edit_mode == 1)
    { // PanH
        exit_pan_h_mode();
    }
    else if (g_gps_state.edit_mode == 2)
    { // PanV
        exit_pan_v_mode();
    }
}

static void action_pan_step(ControlId axis_id, int32_t step)
{
    if (!is_alive())
    {
        return;
    }
    if (axis_id == ControlId::PanHIndicator)
    {
        g_gps_state.pan_x += step * gps_ui::kMapPanStep;
    }
    else if (axis_id == ControlId::PanVIndicator)
    {
        g_gps_state.pan_y += step * gps_ui::kMapPanStep;
    }

    g_gps_state.pending_refresh = true;
    if (g_gps_state.map != NULL)
    {
        lv_obj_invalidate(g_gps_state.map);
    }
}

void zoom_popup_handle_rotary(int32_t diff)
{
    if (!is_alive())
    {
        return;
    }

    updateUserActivity();
    bool zoom_changed = false;

    if (diff > 0)
    {
        if (g_gps_state.popup_zoom < gps_ui::kMaxZoom)
        {
            g_gps_state.popup_zoom++;
            zoom_changed = true;
        }
    }
    else if (diff < 0)
    {
        if (g_gps_state.popup_zoom > gps_ui::kMinZoom)
        {
            g_gps_state.popup_zoom--;
            zoom_changed = true;
        }
    }

    if (zoom_changed)
    {
        if (g_gps_state.popup_label != NULL)
        {
            char zoom_text[32];
            snprintf(zoom_text, sizeof(zoom_text), "%d", g_gps_state.popup_zoom);
            lv_label_set_text(g_gps_state.popup_label, zoom_text);
            lv_obj_invalidate(g_gps_state.popup_label);
            GPS_LOG("[GPS] Selected zoom changed to %d\n", g_gps_state.popup_zoom);
        }
    }
}

void zoom_popup_handle_key(lv_key_t key, lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    extern void hide_zoom_popup();
    extern void update_map_tiles(bool lightweight);
    extern void update_map_anchor();
    extern void update_resolution_display();
    extern void update_zoom_btn();

    if (key == LV_KEY_ESC)
    {
        GPS_LOG("[GPS] ESC key on win: Canceling zoom selection\n");
        hide_zoom_popup();
        return;
    }

    static bool last_pressed_state = false;
    bool pressed = indev_is_pressed(e);
    bool rising_edge = pressed && !last_pressed_state;
    last_pressed_state = pressed;

    if (key == LV_KEY_ENTER || (key == ENCODER_KEY_ROTATE_UP && rising_edge))
    {
        GPS_LOG("[GPS] ENTER key: Applying zoom level %d\n", g_gps_state.popup_zoom);

        double center_lat = g_gps_state.lat;
        double center_lng = g_gps_state.lng;

        if (g_gps_state.anchor.valid)
        {
            get_screen_center_lat_lng(g_gps_state.tile_ctx, center_lat, center_lng);
            GPS_LOG("[GPS] Screen center before zoom: lat=%.6f, lng=%.6f\n", center_lat, center_lng);
        }
        else if (!g_gps_state.has_fix)
        {
            center_lat = gps_ui::kDefaultLat;
            center_lng = gps_ui::kDefaultLng;
            GPS_LOG("[GPS] Using London as default center (no anchor, no GPS fix)\n");
        }

        g_gps_state.zoom_level = g_gps_state.popup_zoom;
        g_gps_state.lat = center_lat;
        g_gps_state.lng = center_lng;
        g_gps_state.pan_x = 0;
        g_gps_state.pan_y = 0;

        g_gps_state.last_resolution_zoom = g_gps_state.zoom_level;
        g_gps_state.last_resolution_lat = g_gps_state.lat;

        update_resolution_display();
        update_map_anchor();
        update_map_tiles(false);
        update_zoom_btn();
        hide_zoom_popup();

        GPS_LOG("[GPS] Zoom applied: level=%d, center=(%.6f, %.6f)\n",
                g_gps_state.zoom_level, g_gps_state.lat, g_gps_state.lng);
        return;
    }

    // 只处理实际的 encoder keycode
    int32_t diff = 0;
    if (key == ENCODER_KEY_ROTATE_DOWN)
    {
        diff = 1;
    }
    else if (key == ENCODER_KEY_ROTATE_UP)
    {
        diff = -1;
    }
    else
    {
        GPS_LOG("[GPS] zoom_popup_handle_key: unexpected keycode=%d (expected %d or %d), ignoring\n",
                key, ENCODER_KEY_ROTATE_UP, ENCODER_KEY_ROTATE_DOWN);
        return;
    }

    if (diff != 0)
    {
        updateUserActivity();
        bool zoom_changed = false;

        if (diff > 0)
        {
            if (g_gps_state.popup_zoom < gps_ui::kMaxZoom)
            {
                g_gps_state.popup_zoom++;
                zoom_changed = true;
            }
        }
        else if (diff < 0)
        {
            if (g_gps_state.popup_zoom > gps_ui::kMinZoom)
            {
                g_gps_state.popup_zoom--;
                zoom_changed = true;
            }
        }

        if (zoom_changed)
        {
            if (g_gps_state.popup_label != NULL)
            {
                char zoom_text[32];
                snprintf(zoom_text, sizeof(zoom_text), "%d", g_gps_state.popup_zoom);
                lv_label_set_text(g_gps_state.popup_label, zoom_text);
                lv_obj_invalidate(g_gps_state.popup_label);
                GPS_LOG("[GPS] Selected zoom changed to %d (via key)\n", g_gps_state.popup_zoom);
            }
        }
    }
}
