#include "gps_page_components.h"
#include "gps_state.h"
#include "gps_page_input.h"
#include "gps_modal.h"
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

// ============================================================================
// Loading Component
// ============================================================================

void show_loading()
{
    if (g_gps_state.loading_msgbox != NULL || g_gps_state.page == NULL) {
        return;
    }
    
    g_gps_state.loading_msgbox = lv_obj_create(g_gps_state.page);
    lv_obj_set_size(g_gps_state.loading_msgbox, 150, 80);
    lv_obj_set_style_bg_color(g_gps_state.loading_msgbox, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_gps_state.loading_msgbox, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_gps_state.loading_msgbox, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_gps_state.loading_msgbox, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_gps_state.loading_msgbox, 20, LV_PART_MAIN);
    lv_obj_center(g_gps_state.loading_msgbox);
    
    lv_obj_t *loading_label = lv_label_create(g_gps_state.loading_msgbox);
    lv_label_set_text(loading_label, "Loading...");
    lv_obj_set_style_text_color(loading_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(loading_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_center(loading_label);
}

void hide_loading()
{
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
    hide_toast();
}

void show_toast(const char* message, uint32_t duration_ms)
{
    hide_toast();
    
    if (g_gps_state.page == NULL) {
        return;
    }
    
    g_gps_state.toast_msgbox = lv_obj_create(g_gps_state.page);
    lv_obj_set_size(g_gps_state.toast_msgbox, 200, 60);
    lv_obj_set_style_bg_color(g_gps_state.toast_msgbox, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_gps_state.toast_msgbox, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_gps_state.toast_msgbox, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_gps_state.toast_msgbox, lv_color_hex(0x666666), LV_PART_MAIN);
    lv_obj_set_style_radius(g_gps_state.toast_msgbox, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_gps_state.toast_msgbox, 12, LV_PART_MAIN);
    lv_obj_center(g_gps_state.toast_msgbox);
    
    lv_obj_t *toast_label = lv_label_create(g_gps_state.toast_msgbox);
    lv_label_set_text(toast_label, message);
    lv_obj_set_style_text_color(toast_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(toast_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(toast_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(toast_label);
    
    if (g_gps_state.toast_timer != NULL) {
        lv_timer_del(g_gps_state.toast_timer);
    }
    g_gps_state.toast_timer = lv_timer_create(toast_timer_cb, duration_ms, NULL);
    lv_timer_set_repeat_count(g_gps_state.toast_timer, 1);
}

void hide_toast()
{
    if (g_gps_state.toast_timer != NULL) {
        lv_timer_del(g_gps_state.toast_timer);
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
    GPS_LOG("[GPS] show_pan_h_indicator: called, pan_h_indicator=%p, map=%p\n", 
            g_gps_state.pan_h_indicator, g_gps_state.map);
    if (g_gps_state.pan_h_indicator != NULL || g_gps_state.map == NULL) {
        GPS_LOG("[GPS] show_pan_h_indicator: early return (already shown or map not ready)\n");
        return;
    }
    
    GPS_LOG("[GPS] show_pan_h_indicator: creating indicator label...\n");
    
    g_gps_state.pan_h_indicator = lv_label_create(g_gps_state.map);
    GPS_LOG("[GPS] show_pan_h_indicator: indicator created at %p\n", g_gps_state.pan_h_indicator);
    
    char indicator_text[64];
    const char* left_arrow = "←";
    const char* right_arrow = "→";
    snprintf(indicator_text, sizeof(indicator_text), "%s ------------ %s", 
             left_arrow, right_arrow);
    lv_label_set_text(g_gps_state.pan_h_indicator, indicator_text);
    
    lv_obj_set_style_text_color(g_gps_state.pan_h_indicator, lv_color_hex(0x00AAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_gps_state.pan_h_indicator, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(g_gps_state.pan_h_indicator, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_gps_state.pan_h_indicator, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_gps_state.pan_h_indicator, 8, LV_PART_MAIN);
    
    lv_obj_align(g_gps_state.pan_h_indicator, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    // CRITICAL: 设置标志，使 indicator 可以接收焦点和事件
    lv_obj_add_flag(g_gps_state.pan_h_indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_gps_state.pan_h_indicator, LV_OBJ_FLAG_SCROLLABLE);
    
    set_control_id(g_gps_state.pan_h_indicator, ControlId::PanHIndicator);
    
    // CRITICAL: 先加入 group，才能接收焦点
    extern lv_group_t *app_g;
    if (app_g != NULL) {
        lv_group_add_obj(app_g, g_gps_state.pan_h_indicator);
        GPS_LOG("[GPS] pan_h_indicator added to group, indicator=%p\n", g_gps_state.pan_h_indicator);
    }
    
    // 绑定所有需要的事件
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
    
    GPS_LOG("[GPS] Horizontal pan indicator shown at bottom center\n");
}

void hide_pan_h_indicator()
{
    if (g_gps_state.pan_h_indicator == NULL) {
        return;
    }
    
    extern lv_group_t *app_g;
    if (app_g != NULL) {
        lv_group_remove_obj(g_gps_state.pan_h_indicator);
    }
    
    lv_obj_del(g_gps_state.pan_h_indicator);
    g_gps_state.pan_h_indicator = nullptr;
    
    GPS_LOG("[GPS] Horizontal pan indicator hidden\n");
}

void show_pan_v_indicator()
{
    GPS_LOG("[GPS] show_pan_v_indicator: called, pan_v_indicator=%p, map=%p\n", 
            g_gps_state.pan_v_indicator, g_gps_state.map);
    if (g_gps_state.pan_v_indicator != NULL || g_gps_state.map == NULL) {
        GPS_LOG("[GPS] show_pan_v_indicator: early return (already shown or map not ready)\n");
        return;
    }
    
    GPS_LOG("[GPS] show_pan_v_indicator: creating indicator label...\n");
    
    g_gps_state.pan_v_indicator = lv_label_create(g_gps_state.map);
    GPS_LOG("[GPS] show_pan_v_indicator: indicator created at %p\n", g_gps_state.pan_v_indicator);
    
    char indicator_text[64];
    const char* up_arrow = "↑";
    const char* down_arrow = "↓";
    snprintf(indicator_text, sizeof(indicator_text), "%s\n|\n|\n|\n|\n|\n%s", 
             up_arrow, down_arrow);
    lv_label_set_text(g_gps_state.pan_v_indicator, indicator_text);
    
    lv_obj_set_style_text_color(g_gps_state.pan_v_indicator, lv_color_hex(0x00AAFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_gps_state.pan_v_indicator, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(g_gps_state.pan_v_indicator, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_gps_state.pan_v_indicator, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_gps_state.pan_v_indicator, 8, LV_PART_MAIN);
    
    lv_obj_align(g_gps_state.pan_v_indicator, LV_ALIGN_LEFT_MID, 20, 0);
    
    // CRITICAL: 设置标志，使 indicator 可以接收焦点和事件
    lv_obj_add_flag(g_gps_state.pan_v_indicator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_gps_state.pan_v_indicator, LV_OBJ_FLAG_SCROLLABLE);
    
    set_control_id(g_gps_state.pan_v_indicator, ControlId::PanVIndicator);
    
    // CRITICAL: 先加入 group，才能接收焦点
    extern lv_group_t *app_g;
    if (app_g != NULL) {
        lv_group_add_obj(app_g, g_gps_state.pan_v_indicator);
        GPS_LOG("[GPS] pan_v_indicator added to group, indicator=%p\n", g_gps_state.pan_v_indicator);
    }
    
    // 绑定所有需要的事件
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
    
    GPS_LOG("[GPS] Vertical pan indicator shown at left center\n");
}

void hide_pan_v_indicator()
{
    if (g_gps_state.pan_v_indicator == NULL) {
        return;
    }
    
    extern lv_group_t *app_g;
    if (app_g != NULL) {
        lv_group_remove_obj(g_gps_state.pan_v_indicator);
    }
    
    lv_obj_del(g_gps_state.pan_v_indicator);
    g_gps_state.pan_v_indicator = nullptr;
    
    GPS_LOG("[GPS] Vertical pan indicator hidden\n");
}

// ============================================================================
// ZoomModal Component
// ============================================================================

static void build_zoom_popup_ui(lv_obj_t* win)
{
    lv_obj_t *title_bar = lv_obj_create(win);
    lv_obj_set_size(title_bar, LV_PCT(100), 35);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x2C2C2C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(title_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(title_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(title_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(title_bar, 0, LV_PART_MAIN);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);
    
    lv_obj_t *title_label = lv_label_create(title_bar);
    lv_label_set_text(title_label, "select level");
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(title_label);
    
    lv_obj_t *content_area = lv_obj_create(win);
    lv_obj_set_size(content_area, LV_PCT(100), LV_PCT(100) - 35);
    lv_obj_set_style_bg_opa(content_area, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content_area, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content_area, 0, LV_PART_MAIN);
    lv_obj_align(content_area, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    g_gps_state.popup_label = lv_label_create(content_area);
    char zoom_text[32];
    snprintf(zoom_text, sizeof(zoom_text), "%d", g_gps_state.popup_zoom);
    lv_label_set_text(g_gps_state.popup_label, zoom_text);
    
    lv_obj_set_style_text_color(g_gps_state.popup_label, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_gps_state.popup_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_align(g_gps_state.popup_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(g_gps_state.popup_label);
    
    lv_obj_add_flag(g_gps_state.popup_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(g_gps_state.popup_label, lv_color_hex(0xF0F0F0), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(g_gps_state.popup_label, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(g_gps_state.popup_label, 3, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(g_gps_state.popup_label, lv_color_hex(0x00AAFF), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(g_gps_state.popup_label, 6, LV_STATE_FOCUSED);
    lv_obj_set_style_radius(g_gps_state.popup_label, 8, LV_STATE_FOCUSED);
    
    GPS_LOG("[GPS] Created popup_label with text='%s', zoom=%d\n", zoom_text, g_gps_state.popup_zoom);
}

void show_zoom_popup()
{
    extern lv_group_t *app_g;
    if (!modal_open(g_gps_state.zoom_modal, lv_screen_active(), app_g)) {
        return;
    }
    
    g_gps_state.popup_zoom = g_gps_state.zoom_level;
    GPS_LOG("[GPS] show_zoom_popup: initializing popup_zoom=%d (from zoom_level=%d)\n", 
            g_gps_state.popup_zoom, g_gps_state.zoom_level);
    
    build_zoom_popup_ui(g_gps_state.zoom_modal.win);
    
    if (g_gps_state.popup_label != NULL) {
        const char* btn_text = lv_label_get_text(g_gps_state.popup_label);
        GPS_LOG("[GPS] popup_label created, text='%s', visible=%d\n", 
                btn_text ? btn_text : "(null)", lv_obj_has_flag(g_gps_state.popup_label, LV_OBJ_FLAG_HIDDEN) ? 0 : 1);
    } else {
        GPS_LOG("[GPS] ERROR: popup_label is NULL after build_zoom_popup_ui!\n");
    }
    
    if (g_gps_state.popup_label != NULL && g_gps_state.zoom_modal.group != NULL) {
        lv_group_remove_all_objs(g_gps_state.zoom_modal.group);
        lv_group_add_obj(g_gps_state.zoom_modal.group, g_gps_state.popup_label);
        set_default_group(g_gps_state.zoom_modal.group);
        bind_encoder_to_group(g_gps_state.zoom_modal.group);
        lv_group_focus_obj(g_gps_state.popup_label);
        lv_group_set_editing(g_gps_state.zoom_modal.group, true);
        lv_obj_invalidate(g_gps_state.popup_label);
        
        lv_obj_t* focused = lv_group_get_focused(g_gps_state.zoom_modal.group);
        if (focused != g_gps_state.popup_label) {
            GPS_LOG("[GPS] ERROR: Focus mismatch! focused=%p, expected=%p\n", 
                    focused, g_gps_state.popup_label);
        } else {
            GPS_LOG("[GPS] popup_label focused correctly, encoder bound, editing enabled\n");
        }
    } else {
        GPS_LOG("[GPS] ERROR: popup_label=%p or group=%p is NULL!\n", 
                g_gps_state.popup_label, g_gps_state.zoom_modal.group);
    }
    
    if (g_gps_state.popup_label != NULL) {
        set_control_id(g_gps_state.popup_label, ControlId::ZoomValueLabel);
        lv_obj_add_event_cb(g_gps_state.popup_label, on_ui_event, LV_EVENT_ROTARY, NULL);
        lv_obj_add_event_cb(g_gps_state.popup_label, on_ui_event, LV_EVENT_KEY, NULL);
        GPS_LOG("[GPS] Event callbacks added to popup_label\n");
    }
    
    if (!g_gps_state.zoom_win_cb_bound) {
        set_control_id(g_gps_state.zoom_modal.win, ControlId::ZoomWin);
        lv_obj_add_event_cb(g_gps_state.zoom_modal.win, on_ui_event, LV_EVENT_ROTARY, NULL);
        lv_obj_add_event_cb(g_gps_state.zoom_modal.win, on_ui_event, LV_EVENT_KEY, NULL);
        lv_obj_add_flag(g_gps_state.zoom_modal.win, LV_OBJ_FLAG_CLICKABLE);
        g_gps_state.zoom_win_cb_bound = true;
    }
    
    GPS_LOG("[GPS] Zoom popup opened, current zoom=%d, selected zoom=%d\n", 
            g_gps_state.zoom_level, g_gps_state.popup_zoom);
}

void hide_zoom_popup()
{
    modal_close(g_gps_state.zoom_modal);
    g_gps_state.popup_label = nullptr;

    extern lv_group_t *app_g;
    if (app_g != NULL) {
        lv_group_set_editing(app_g, false);
        set_default_group(app_g);
        bind_encoder_to_group(app_g);

        if (g_gps_state.zoom != NULL) {
            lv_group_focus_obj(g_gps_state.zoom);
            GPS_LOG("[GPS] Focus restored to zoom button after popup close\n");
        } else {
            GPS_LOG("[GPS] WARNING: zoom button is NULL, cannot restore focus\n");
        }
    }
    
    GPS_LOG("[GPS] Zoom popup hidden\n");
}

// ============================================================================
// UI Layout Helper
// ============================================================================

void fix_ui_elements_position()
{
    if (g_gps_state.map == NULL) {
        return;
    }
    
    if (g_gps_state.panel != NULL) {
        lv_obj_align(g_gps_state.panel, LV_ALIGN_TOP_RIGHT, 0, 3);
        lv_obj_move_foreground(g_gps_state.panel);
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

// ============================================================================
// Style Helpers
// ============================================================================

void style_control_button(lv_obj_t* btn, lv_color_t bg)
{
    lv_obj_set_style_bg_color(btn, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xEBA341), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);

    lv_obj_set_style_bg_color(btn, lv_color_hex(0xEBA341), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(btn, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(btn, 0, LV_STATE_FOCUSED);

    lv_obj_set_style_bg_color(btn, lv_color_hex(0xF1B65A), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, LV_STATE_PRESSED);

    lv_obj_set_size(btn, 80, 32);
}

void style_popup_window(lv_obj_t* win)
{
    lv_obj_set_style_bg_color(win, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 10, LV_PART_MAIN);
    lv_obj_set_style_outline_width(win, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(win, lv_color_hex(0x00AAFF), LV_STATE_FOCUSED);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);
}
