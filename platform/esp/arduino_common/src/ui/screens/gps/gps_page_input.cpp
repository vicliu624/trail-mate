#include "ui/screens/gps/gps_page_input.h"
#include "platform/ui/gps_runtime.h"
#include "screen_sleep.h"
#include "sys/clock.h"
#include "ui/page/page_profile.h"
#include "ui/screens/gps/gps_constants.h"
#include "ui/screens/gps/gps_modal.h"
#include "ui/screens/gps/gps_page_lifetime.h"
#include "ui/screens/gps/gps_page_map.h"
#include "ui/screens/gps/gps_route_overlay.h"
#include "ui/screens/gps/gps_state.h"
#include "ui/screens/gps/gps_tracker_overlay.h"
#include "ui/ui_common.h"
#include "ui/widgets/map/map_tiles.h"
#include <cstdint>
#include <cstdio>

#define GPS_DEBUG 0 // Enable debug logging
#if GPS_DEBUG
#define GPS_LOG(...) std::printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

#define GPS_FLOW_LOG(...)         \
    do                            \
    {                             \
        std::printf(__VA_ARGS__); \
        std::fflush(stdout);      \
    } while (0)

extern GPSPageState g_gps_state;

using gps::ui::lifetime::is_alive;

using GPSData = platform::ui::gps::GpsState;

static bool show_pan_axis_controls()
{
    return !::ui::page_profile::current().large_touch_hitbox;
}

static bool is_pan_h_editing()
{
    return g_gps_state.edit_mode == GpsEditMode::PanH;
}

static bool is_pan_v_editing()
{
    return g_gps_state.edit_mode == GpsEditMode::PanV;
}

static bool is_pan_editing()
{
    return is_pan_h_editing() || is_pan_v_editing();
}

static int gps_edit_mode_value()
{
    return static_cast<int>(g_gps_state.edit_mode);
}

static bool is_arrow_key(lv_key_t key)
{
    return key == LV_KEY_LEFT || key == LV_KEY_RIGHT || key == LV_KEY_UP || key == LV_KEY_DOWN;
}

static bool adjust_popup_zoom(int32_t diff)
{
    if (diff > 0)
    {
        if (g_gps_state.popup_zoom >= gps_ui::kMaxZoom)
        {
            return false;
        }
        ++g_gps_state.popup_zoom;
        return true;
    }

    if (diff < 0)
    {
        if (g_gps_state.popup_zoom <= gps_ui::kMinZoom)
        {
            return false;
        }
        --g_gps_state.popup_zoom;
        return true;
    }

    return false;
}

enum class InputStepMode : uint8_t
{
    OneDimensional = 0,
};

bool map_input_step_from_key(lv_key_t key, InputStepMode mode, int32_t* out_step)
{
    if (out_step == nullptr)
    {
        return false;
    }

    switch (mode)
    {
    case InputStepMode::OneDimensional:
        if (key == LV_KEY_RIGHT || key == LV_KEY_UP)
        {
            *out_step = +1;
            return true;
        }
        if (key == LV_KEY_LEFT || key == LV_KEY_DOWN)
        {
            *out_step = -1;
            return true;
        }
        break;

    }

    return false;
}

ControlId ctrl_id(lv_obj_t* obj)
{
    if (obj == nullptr) return ControlId::Page;
    auto* tag = (ControlTag*)lv_obj_get_user_data(obj);
    return tag ? tag->id : ControlId::Page;
}

static void action_pan_step(ControlId axis_id, int32_t step);

static lv_indev_t* resolve_event_indev(lv_event_t* e)
{
    if (e != nullptr)
    {
        if (lv_indev_t* indev = lv_event_get_indev(e))
        {
            return indev;
        }
    }
    return lv_indev_active();
}

static bool event_from_encoder(lv_event_t* e)
{
    lv_indev_t* indev = resolve_event_indev(e);
    return indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER;
}

static bool event_from_keypad(lv_event_t* e)
{
    lv_indev_t* indev = resolve_event_indev(e);
    return indev != nullptr && lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD;
}

static const char* event_input_model_label(lv_event_t* e)
{
    if (event_from_encoder(e))
    {
        return "encoder";
    }
    if (event_from_keypad(e))
    {
        return "keypad";
    }
    return "unknown";
}

static bool try_handle_pan_edit_key(lv_obj_t* target, lv_key_t key, lv_event_t* e, const char* source)
{
    if (!is_pan_editing() || g_gps_state.zoom_modal.is_open())
    {
        return false;
    }

    if (!is_arrow_key(key))
    {
        return false;
    }

    const bool horizontal = is_pan_h_editing();
    const char* axis = horizontal ? "h" : "v";
    const ControlId axis_id = horizontal ? ControlId::PanHIndicator : ControlId::PanVIndicator;
    const int target_id = static_cast<int>(ctrl_id(target));
    const char* input_model = event_input_model_label(e);

    int32_t step = 0;
    if (!map_input_step_from_key(key, InputStepMode::OneDimensional, &step))
    {
        GPS_FLOW_LOG("[GPS][MAP][input] pan_key_ignored axis=%s src=%s indev=%s target=%d key=%d mode=%d\n",
                     axis,
                     source,
                     input_model,
                     target_id,
                     key,
                     gps_edit_mode_value());
        return true;
    }

    const int32_t before_x = g_gps_state.pan_x;
    const int32_t before_y = g_gps_state.pan_y;
    action_pan_step(axis_id, step);
    GPS_FLOW_LOG("[GPS][MAP][input] pan_key axis=%s src=%s indev=%s target=%d key=%d step=%d pan=%d,%d->%d,%d\n",
                 axis,
                 source,
                 input_model,
                 target_id,
                 key,
                 step,
                 before_x,
                 before_y,
                 g_gps_state.pan_x,
                 g_gps_state.pan_y);
    return true;
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
static void action_layer_open_popup();
static void action_route_focus();
static void refresh_map_after_pan_step();

void zoom_popup_handle_rotary(int32_t diff);
void zoom_popup_handle_key(lv_key_t key, lv_event_t* e);

static void refresh_map_after_pan_step()
{
    extern void update_map_tiles(bool lightweight);

    g_gps_state.pending_refresh = false;
    update_map_tiles(false);
    log_map_tile_state("pan_step");
}

static void exit_pan_h_mode()
{
    extern void hide_pan_h_indicator();
    g_gps_state.edit_mode = GpsEditMode::None;
    hide_pan_h_indicator();

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
    g_gps_state.edit_mode = GpsEditMode::None;
    hide_pan_v_indicator();

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

// Edge detection is handled in poll_encoder_for_pan().
void on_ui_event(lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    auto code = lv_event_get_code(e);
    auto* target = (lv_obj_t*)lv_event_get_target(e);

    // ============================================================================
    // DEBUG: detailed event trace
    // ============================================================================
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
                key, target, (int)target_id, focused, editing, is_pan_h_editing(), is_pan_v_editing());
    }
    else if (code == LV_EVENT_ROTARY)
    {
        int32_t diff = lv_event_get_rotary_diff(e);
        GPS_LOG("[GPS] EVENT: ROTARY, diff=%d, target=%p(id=%d), focused=%p, editing=%d, pan_h=%d, pan_v=%d\n",
                diff, target, (int)target_id, focused, editing, is_pan_h_editing(), is_pan_v_editing());
    }
    else if (code == LV_EVENT_CLICKED)
    {
        GPS_LOG("[GPS] EVENT: CLICKED, target=%p(id=%d), focused=%p, editing=%d, pan_h=%d, pan_v=%d\n",
                target, (int)target_id, focused, editing, is_pan_h_editing(), is_pan_v_editing());
    }
    else if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED)
    {
        GPS_LOG("[GPS] EVENT: %s, target=%p(id=%d), focused=%p, editing=%d, pan_h=%d, pan_v=%d\n",
                code == LV_EVENT_PRESSED ? "PRESSED" : "RELEASED",
                target, (int)target_id, focused, editing, is_pan_h_editing(), is_pan_v_editing());
    }

    // Handle indicator-local key/rotary events early so focused pan overlays stay responsive.
    if (target == g_gps_state.pan_h_indicator)
    {
        if (g_gps_state.zoom_modal.is_open()) return;
        updateUserActivity();

        // Focused pan overlays can receive keypad keys or encoder-generated keys.
        if (code == LV_EVENT_KEY)
        {
            if (try_handle_pan_edit_key(target, (lv_key_t)lv_event_get_key(e), e, "indicator"))
            {
                return;
            }
            return;
        }

        if (code == LV_EVENT_ROTARY && is_pan_h_editing())
        {
            handle_rotary(target, lv_event_get_rotary_diff(e));
            return;
        }

        // CLICKED is handled by pan_indicator_event_cb.
        return;
    }

    if (target == g_gps_state.pan_v_indicator)
    {
        if (g_gps_state.zoom_modal.is_open()) return;
        updateUserActivity();

        // Focused pan overlays can receive keypad keys or encoder-generated keys.
        if (code == LV_EVENT_KEY)
        {
            if (try_handle_pan_edit_key(target, (lv_key_t)lv_event_get_key(e), e, "indicator"))
            {
                return;
            }
            return;
        }

        if (code == LV_EVENT_ROTARY && is_pan_v_editing())
        {
            handle_rotary(target, lv_event_get_rotary_diff(e));
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
    if (modal_is_open(g_gps_state.layer_modal) && !is_back_btn)
    {
        return;
    }

    switch (code)
    {
    case LV_EVENT_CLICKED:
        handle_click(target);
        break;
    case LV_EVENT_ROTARY:
        handle_rotary(target, lv_event_get_rotary_diff(e));
        break;
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
            g_gps_state.edit_mode = GpsEditMode::None;
            extern void hide_pan_h_indicator();
            hide_pan_h_indicator();
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
            g_gps_state.edit_mode = GpsEditMode::None;
            extern void hide_pan_v_indicator();
            hide_pan_v_indicator();
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

    GPS_LOG("[GPS] handle_click: id=%d, edit_mode=%d\n", (int)id, gps_edit_mode_value());

    switch (id)
    {
    case ControlId::BackBtn:
        if (modal_is_open(g_gps_state.layer_modal))
        {
            extern void hide_layer_popup();
            hide_layer_popup();
            break;
        }
        action_back_exit();
        break;
    case ControlId::ZoomBtn:
        action_zoom_open_popup();
        break;

    case ControlId::PosBtn:
        action_position_center();
        break;

    case ControlId::PanHBtn:
        if (is_pan_h_editing())
        {
            action_pan_exit();
        }
        else
        {
            action_pan_enter(ControlId::PanHBtn);
        }
        break;

    case ControlId::PanVBtn:
        if (is_pan_v_editing())
        {
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

    case ControlId::LayerBtn:
        action_layer_open_popup();
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
            (int)id, gps_edit_mode_value(), diff);

    if (g_gps_state.zoom_modal.is_open())
    {
        if (id == ControlId::ZoomValueLabel || id == ControlId::ZoomWin)
        {
            extern void zoom_popup_handle_rotary(int32_t diff);
            zoom_popup_handle_rotary(diff);
            return;
        }
    }

    // When in pan editing mode, handle rotary regardless of target.
    // This keeps the edit-mode input path target-independent.
    if (is_pan_h_editing())
    {
        GPS_LOG("[GPS] handle_rotary: PanH editing mode, calling action_pan_step\n");
        action_pan_step(ControlId::PanHIndicator, diff);
        return;
    }

    if (is_pan_v_editing())
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

    if (modal_is_open(g_gps_state.layer_modal) &&
        (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE))
    {
        extern void hide_layer_popup();
        hide_layer_popup();
        return;
    }

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
    if (modal_is_open(g_gps_state.layer_modal))
    {
        return;
    }

    if (try_handle_pan_edit_key(target, key, e, "handle_key"))
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
            if (is_pan_h_editing())
            {
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
            if (is_pan_v_editing())
            {
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
        if (id == ControlId::LayerBtn)
        {
            action_layer_open_popup();
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

    if (show_pan_axis_controls() && (key == 'h' || key == 'H'))
    {
        if (is_pan_h_editing())
        {
            action_pan_exit();
        }
        else
        {
            action_pan_enter(ControlId::PanHBtn);
        }
        return;
    }

    if (show_pan_axis_controls() && (key == 'v' || key == 'V'))
    {
        if (is_pan_v_editing())
        {
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

    if (key == 'l' || key == 'L')
    {
        action_layer_open_popup();
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
    uint32_t now = sys::millis_now();
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
    GPSData gps_data = platform::ui::gps::get_data();

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
    g_gps_state.follow_position = true;

    // Create or update GPS marker
    create_gps_marker();

    // Update map to show GPS position centered
    update_map_tiles(false);
    log_map_tile_state("position_center");
    GPS_LOG("[GPS] Position action: centered GPS marker at lat=%.6f, lng=%.6f\n",
            g_gps_state.lat, g_gps_state.lng);
}

static void action_route_focus()
{
    if (!is_alive())
    {
        return;
    }
    if (::ui::page_profile::current().large_touch_hitbox)
    {
        extern void show_route_popup();
        show_route_popup();
        return;
    }
    extern void show_toast(const char* message, uint32_t duration_ms);
    if (!gps_route_focus(true))
    {
        show_toast("No route", 1500);
    }
}

static void action_layer_open_popup()
{
    if (!is_alive())
    {
        return;
    }
    extern void show_layer_popup();
    uint32_t now = sys::millis_now();
    if (g_gps_state.layer_modal.close_ms > 0 &&
        (now - g_gps_state.layer_modal.close_ms) < 300)
    {
        return;
    }
    if (!g_gps_state.layer_modal.is_open())
    {
        show_layer_popup();
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
        g_gps_state.edit_mode = GpsEditMode::PanH;
        hide_pan_v_indicator();
        show_pan_h_indicator();

        if (app_g != NULL && g_gps_state.pan_h_indicator != NULL)
        {
            set_default_group(app_g);
            bind_navigation_inputs_to_group(app_g);

            // Ensure indicator is focused in group editing mode.
            lv_group_focus_obj(g_gps_state.pan_h_indicator);
            lv_group_set_editing(app_g, true);

            // Focus diagnostics.
            lv_obj_t* f = lv_group_get_focused(app_g);
            bool editing = lv_group_get_editing(app_g);
            GPS_FLOW_LOG("[GPS][MAP][input] pan_mode enter axis=h focus=%p indicator=%p editing=%d\n",
                         f,
                         g_gps_state.pan_h_indicator,
                         editing);
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
        g_gps_state.edit_mode = GpsEditMode::PanV;
        hide_pan_h_indicator();
        show_pan_v_indicator();

        if (app_g != NULL && g_gps_state.pan_v_indicator != NULL)
        {
            set_default_group(app_g);
            bind_navigation_inputs_to_group(app_g);
            lv_group_focus_obj(g_gps_state.pan_v_indicator);
            lv_group_set_editing(app_g, true);

            // Focus diagnostics.
            lv_obj_t* f = lv_group_get_focused(app_g);
            GPS_FLOW_LOG("[GPS][MAP][input] pan_mode enter axis=v focus=%p indicator=%p editing=%d\n",
                         f,
                         g_gps_state.pan_v_indicator,
                         lv_group_get_editing(app_g));
            GPS_LOG("[GPS] Vertical pan: editing mode ON, focus=%p, indicator=%p, editing=%d\n",
                    f, g_gps_state.pan_v_indicator, lv_group_get_editing(app_g));
        }
    }
}

static void action_pan_exit()
{
    if (g_gps_state.edit_mode == GpsEditMode::PanH)
    {
        exit_pan_h_mode();
    }
    else if (g_gps_state.edit_mode == GpsEditMode::PanV)
    {
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
        g_gps_state.follow_position = false;
    }
    else if (axis_id == ControlId::PanVIndicator)
    {
        g_gps_state.pan_y += step * gps_ui::kMapPanStep;
        g_gps_state.follow_position = false;
    }

    refresh_map_after_pan_step();
}

void zoom_popup_sync_widgets()
{
    if (g_gps_state.popup_label != NULL && ::ui::page_profile::current().large_touch_hitbox)
    {
        extern void zoom_popup_sync_touch_selection();
        zoom_popup_sync_touch_selection();
        lv_obj_invalidate(g_gps_state.popup_label);
    }
    else if (g_gps_state.popup_label != NULL)
    {
        char zoom_text[32];
        snprintf(zoom_text, sizeof(zoom_text), "%d", g_gps_state.popup_zoom);
        lv_label_set_text(g_gps_state.popup_label, zoom_text);
        lv_obj_invalidate(g_gps_state.popup_label);
    }

    if (g_gps_state.popup_roller != NULL)
    {
        const uint16_t selected = static_cast<uint16_t>(g_gps_state.popup_zoom - gps_ui::kMinZoom);
        if (lv_roller_get_selected(g_gps_state.popup_roller) != selected)
        {
            lv_roller_set_selected(g_gps_state.popup_roller, selected, LV_ANIM_OFF);
        }
        lv_obj_invalidate(g_gps_state.popup_roller);
    }
}

void zoom_popup_apply_selection()
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

    GPS_LOG("[GPS] Applying zoom level %d\n", g_gps_state.popup_zoom);

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

    const int previous_zoom = g_gps_state.zoom_level;
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
    GPS_FLOW_LOG("[GPS][MAP][flow] zoom_apply from=%d to=%d center=%.6f,%.6f anchor=%d\n",
                 previous_zoom,
                 g_gps_state.zoom_level,
                 g_gps_state.lat,
                 g_gps_state.lng,
                 g_gps_state.anchor.valid);
    log_map_tile_state("zoom_apply");
    update_zoom_btn();
    hide_zoom_popup();

    GPS_LOG("[GPS] Zoom applied: level=%d, center=(%.6f, %.6f)\n",
            g_gps_state.zoom_level, g_gps_state.lat, g_gps_state.lng);
}

void zoom_popup_handle_rotary(int32_t diff)
{
    if (!is_alive())
    {
        return;
    }

    updateUserActivity();
    if (adjust_popup_zoom(diff))
    {
        zoom_popup_sync_widgets();
        GPS_LOG("[GPS] Selected zoom changed to %d\n", g_gps_state.popup_zoom);
    }
}

void zoom_popup_handle_key(lv_key_t key, lv_event_t* e)
{
    if (!is_alive())
    {
        return;
    }
    extern void hide_zoom_popup();
    (void)e;

    if (key == LV_KEY_ESC)
    {
        GPS_LOG("[GPS] ESC key on win: Canceling zoom selection\n");
        hide_zoom_popup();
        return;
    }

    if (key == LV_KEY_ENTER)
    {
        zoom_popup_apply_selection();
        return;
    }

    int32_t diff = 0;
    if (!map_input_step_from_key(key, InputStepMode::OneDimensional, &diff))
    {
        GPS_LOG("[GPS] zoom_popup_handle_key: unexpected keycode=%d, ignoring\n", key);
        return;
    }

    if (diff != 0 && adjust_popup_zoom(diff))
    {
        updateUserActivity();
        zoom_popup_sync_widgets();
        GPS_LOG("[GPS] Selected zoom changed to %d (via key)\n", g_gps_state.popup_zoom);
    }
}
