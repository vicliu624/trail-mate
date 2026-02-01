#include "gps_page_components.h"

#include "gps_page_input.h"
#include "gps_page_lifetime.h"
#include "gps_page_styles.h"
#include "gps_modal.h"
#include "gps_state.h"
#include "../../ui_common.h"
#include "lvgl.h"

#include <Arduino.h>
#include <cstdio>

#define GPS_DEBUG 0
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

extern GPSPageState g_gps_state;

using gps::ui::lifetime::is_alive;

// ============================================================================
// Loading Component
// ============================================================================

void show_loading()
{
    if (!gps_ui::kShowLoadingOverlay) {
        return;
    }
    if (!is_alive() || g_gps_state.loading_msgbox != NULL || g_gps_state.page == NULL) {
        return;
    }

    g_gps_state.loading_msgbox = lv_obj_create(g_gps_state.page);
    lv_obj_set_size(g_gps_state.loading_msgbox, 150, 80);
    gps::ui::styles::apply_loading_box(g_gps_state.loading_msgbox);
    lv_obj_center(g_gps_state.loading_msgbox);

    lv_obj_t* loading_label = lv_label_create(g_gps_state.loading_msgbox);
    lv_label_set_text(loading_label, "Loading...");
    gps::ui::styles::apply_loading_label(loading_label);
    lv_obj_center(loading_label);
}

void hide_loading()
{
    if (!gps_ui::kShowLoadingOverlay) {
        g_gps_state.loading_msgbox = NULL;
        return;
    }
    if (g_gps_state.loading_msgbox != NULL) {
        lv_obj_del(g_gps_state.loading_msgbox);
        g_gps_state.loading_msgbox = NULL;
    }
}

// ============================================================================
// Toast Component
// ============================================================================

static void toast_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!is_alive()) {
        return;
    }
    hide_toast();
}

void show_toast(const char* message, uint32_t duration_ms)
{
    if (!is_alive()) {
        return;
    }

    hide_toast();

    if (g_gps_state.page == NULL) {
        return;
    }

    g_gps_state.toast_msgbox = lv_obj_create(g_gps_state.page);
    lv_obj_set_size(g_gps_state.toast_msgbox, 200, 60);
    gps::ui::styles::apply_toast_box(g_gps_state.toast_msgbox);
    lv_obj_center(g_gps_state.toast_msgbox);

    lv_obj_t* toast_label = lv_label_create(g_gps_state.toast_msgbox);
    lv_label_set_text(toast_label, message);
    gps::ui::styles::apply_toast_label(toast_label);
    lv_obj_center(toast_label);

    if (g_gps_state.toast_timer != NULL) {
        gps::ui::lifetime::remove_timer(g_gps_state.toast_timer);
        g_gps_state.toast_timer = nullptr;
    }

    g_gps_state.toast_timer = gps::ui::lifetime::add_timer(toast_timer_cb, duration_ms, NULL);
    if (g_gps_state.toast_timer) {
        lv_timer_set_repeat_count(g_gps_state.toast_timer, 1);
    }
}

void hide_toast()
{
    if (g_gps_state.toast_timer != NULL) {
        gps::ui::lifetime::remove_timer(g_gps_state.toast_timer);
        g_gps_state.toast_timer = nullptr;
    }

    if (g_gps_state.toast_msgbox != NULL) {
        lv_obj_del(g_gps_state.toast_msgbox);
        g_gps_state.toast_msgbox = NULL;
    }
}

// ============================================================================
// PanIndicator Component
// ============================================================================

void show_pan_h_indicator()
{
    if (!is_alive()) {
        return;
    }

    GPS_LOG("[GPS] show_pan_h_indicator: called, pan_h_indicator=%p, map=%p\n",
            g_gps_state.pan_h_indicator, g_gps_state.map);
    if (g_gps_state.pan_h_indicator != NULL || g_gps_state.map == NULL) {
        GPS_LOG("[GPS] show_pan_h_indicator: early return (already shown or map not ready)\n");
        return;
    }

    g_gps_state.pan_h_indicator = lv_label_create(g_gps_state.map);

    char indicator_text[64];
    const char* left_arrow = "<";
    const char* right_arrow = ">";
    snprintf(indicator_text, sizeof(indicator_text), "%s ------------ %s", left_arrow, right_arrow);
    lv_label_set_text(g_gps_state.pan_h_indicator, indicator_text);

    gps::ui::styles::apply_indicator_label(g_gps_state.pan_h_indicator);
    lv_obj_align(g_gps_state.pan_h_indicator, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_add_flag(g_gps_state.pan_h_indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_gps_state.pan_h_indicator, LV_OBJ_FLAG_SCROLLABLE);

    set_control_id(g_gps_state.pan_h_indicator, ControlId::PanHIndicator);

    extern lv_group_t* app_g;
    if (app_g != NULL) {
        lv_group_add_obj(app_g, g_gps_state.pan_h_indicator);
    }

    extern void pan_indicator_event_cb(lv_event_t* e);
    extern void on_ui_event(lv_event_t* e);
    lv_obj_add_event_cb(g_gps_state.pan_h_indicator, pan_indicator_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h_indicator, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h_indicator, on_ui_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_h_indicator, on_ui_event, LV_EVENT_ROTARY, NULL);

    lv_obj_clear_flag(g_gps_state.pan_h_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gps_state.pan_h_indicator);
    fix_ui_elements_position();
    lv_obj_update_layout(g_gps_state.map);
    lv_obj_invalidate(g_gps_state.pan_h_indicator);
}

void hide_pan_h_indicator()
{
    if (g_gps_state.pan_h_indicator == NULL) {
        return;
    }

    extern lv_group_t* app_g;
    if (app_g != NULL) {
        lv_group_remove_obj(g_gps_state.pan_h_indicator);
    }

    lv_obj_del(g_gps_state.pan_h_indicator);
    g_gps_state.pan_h_indicator = nullptr;
}

void show_pan_v_indicator()
{
    if (!is_alive()) {
        return;
    }

    GPS_LOG("[GPS] show_pan_v_indicator: called, pan_v_indicator=%p, map=%p\n",
            g_gps_state.pan_v_indicator, g_gps_state.map);
    if (g_gps_state.pan_v_indicator != NULL || g_gps_state.map == NULL) {
        GPS_LOG("[GPS] show_pan_v_indicator: early return (already shown or map not ready)\n");
        return;
    }

    g_gps_state.pan_v_indicator = lv_label_create(g_gps_state.map);

    char indicator_text[64];
    const char* up_arrow = "^";
    const char* down_arrow = "v";
    snprintf(indicator_text, sizeof(indicator_text), "%s\n|\n|\n|\n|\n|\n%s", up_arrow, down_arrow);
    lv_label_set_text(g_gps_state.pan_v_indicator, indicator_text);

    gps::ui::styles::apply_indicator_label(g_gps_state.pan_v_indicator);
    lv_obj_align(g_gps_state.pan_v_indicator, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_add_flag(g_gps_state.pan_v_indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_gps_state.pan_v_indicator, LV_OBJ_FLAG_SCROLLABLE);

    set_control_id(g_gps_state.pan_v_indicator, ControlId::PanVIndicator);

    extern lv_group_t* app_g;
    if (app_g != NULL) {
        lv_group_add_obj(app_g, g_gps_state.pan_v_indicator);
    }

    extern void pan_indicator_event_cb(lv_event_t* e);
    extern void on_ui_event(lv_event_t* e);
    lv_obj_add_event_cb(g_gps_state.pan_v_indicator, pan_indicator_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v_indicator, on_ui_event, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v_indicator, on_ui_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(g_gps_state.pan_v_indicator, on_ui_event, LV_EVENT_ROTARY, NULL);

    lv_obj_clear_flag(g_gps_state.pan_v_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_gps_state.pan_v_indicator);
    fix_ui_elements_position();
    lv_obj_update_layout(g_gps_state.map);
    lv_obj_invalidate(g_gps_state.pan_v_indicator);
}

void hide_pan_v_indicator()
{
    if (g_gps_state.pan_v_indicator == NULL) {
        return;
    }

    extern lv_group_t* app_g;
    if (app_g != NULL) {
        lv_group_remove_obj(g_gps_state.pan_v_indicator);
    }

    lv_obj_del(g_gps_state.pan_v_indicator);
    g_gps_state.pan_v_indicator = nullptr;
}

// ============================================================================
// ZoomModal Component
// ============================================================================

static void build_zoom_popup_ui(lv_obj_t* win)
{
    if (!is_alive() || !win) {
        return;
    }

    gps::ui::styles::apply_zoom_popup_win(win);

    lv_obj_t* title_bar = lv_obj_create(win);
    lv_obj_set_size(title_bar, LV_PCT(100), 35);
    gps::ui::styles::apply_zoom_popup_title_bar(title_bar);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "select level");
    gps::ui::styles::apply_zoom_popup_title_label(title_label);
    lv_obj_center(title_label);

    lv_obj_t* content_area = lv_obj_create(win);
    lv_obj_set_size(content_area, LV_PCT(100), LV_PCT(100) - 35);
    gps::ui::styles::apply_zoom_popup_content_area(content_area);
    lv_obj_align(content_area, LV_ALIGN_BOTTOM_MID, 0, 0);

    g_gps_state.popup_label = lv_label_create(content_area);
    char zoom_text[32];
    snprintf(zoom_text, sizeof(zoom_text), "%d", g_gps_state.popup_zoom);
    lv_label_set_text(g_gps_state.popup_label, zoom_text);

    gps::ui::styles::apply_zoom_popup_value_label(g_gps_state.popup_label);
    lv_obj_center(g_gps_state.popup_label);

    lv_obj_add_flag(g_gps_state.popup_label, LV_OBJ_FLAG_CLICKABLE);
}

void show_zoom_popup()
{
    if (!is_alive()) {
        return;
    }

    extern lv_group_t* app_g;
    if (!modal_open(g_gps_state.zoom_modal, lv_screen_active(), app_g)) {
        return;
    }

    g_gps_state.popup_zoom = g_gps_state.zoom_level;

    build_zoom_popup_ui(g_gps_state.zoom_modal.win);

    if (g_gps_state.popup_label != NULL && g_gps_state.zoom_modal.group != NULL) {
        lv_group_remove_all_objs(g_gps_state.zoom_modal.group);
        lv_group_add_obj(g_gps_state.zoom_modal.group, g_gps_state.popup_label);
        set_default_group(g_gps_state.zoom_modal.group);
        bind_encoder_to_group(g_gps_state.zoom_modal.group);
        lv_group_focus_obj(g_gps_state.popup_label);
        lv_group_set_editing(g_gps_state.zoom_modal.group, true);
        lv_obj_invalidate(g_gps_state.popup_label);
    }

    if (g_gps_state.popup_label != NULL) {
        set_control_id(g_gps_state.popup_label, ControlId::ZoomValueLabel);
        lv_obj_add_event_cb(g_gps_state.popup_label, on_ui_event, LV_EVENT_ROTARY, NULL);
        lv_obj_add_event_cb(g_gps_state.popup_label, on_ui_event, LV_EVENT_KEY, NULL);
    }

    if (!g_gps_state.zoom_win_cb_bound) {
        set_control_id(g_gps_state.zoom_modal.win, ControlId::ZoomWin);
        lv_obj_add_event_cb(g_gps_state.zoom_modal.win, on_ui_event, LV_EVENT_ROTARY, NULL);
        lv_obj_add_event_cb(g_gps_state.zoom_modal.win, on_ui_event, LV_EVENT_KEY, NULL);
        lv_obj_add_flag(g_gps_state.zoom_modal.win, LV_OBJ_FLAG_CLICKABLE);
        g_gps_state.zoom_win_cb_bound = true;
    }
}

void hide_zoom_popup()
{
    if (!g_gps_state.zoom_modal.is_open()) {
        return;
    }

    modal_close(g_gps_state.zoom_modal);
    g_gps_state.popup_label = nullptr;

    extern lv_group_t* app_g;
    if (app_g != NULL) {
        lv_group_set_editing(app_g, false);
        set_default_group(app_g);
        bind_encoder_to_group(app_g);

        if (g_gps_state.zoom != NULL) {
            lv_group_focus_obj(g_gps_state.zoom);
        }
    }
}

// ============================================================================
// UI Layout Helper
// ============================================================================

void fix_ui_elements_position()
{
    if (!is_alive() || g_gps_state.map == NULL) {
        return;
    }

    if (g_gps_state.panel != NULL) {
        lv_obj_align(g_gps_state.panel, LV_ALIGN_TOP_RIGHT, 0, 3);
        lv_obj_move_foreground(g_gps_state.panel);
    }

    if (g_gps_state.member_panel != NULL) {
        lv_obj_align(g_gps_state.member_panel, LV_ALIGN_TOP_LEFT, 0, 3);
        lv_obj_move_foreground(g_gps_state.member_panel);
    }

    if (g_gps_state.resolution_label != NULL) {
        lv_obj_align(g_gps_state.resolution_label, LV_ALIGN_BOTTOM_LEFT, 10, -10);
        lv_obj_move_foreground(g_gps_state.resolution_label);
    }

    if (g_gps_state.pan_h_indicator != NULL) {
        lv_obj_align(g_gps_state.pan_h_indicator, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_move_foreground(g_gps_state.pan_h_indicator);
    }

    if (g_gps_state.pan_v_indicator != NULL) {
        lv_obj_align(g_gps_state.pan_v_indicator, LV_ALIGN_LEFT_MID, 20, 0);
        lv_obj_move_foreground(g_gps_state.pan_v_indicator);
    }
}
