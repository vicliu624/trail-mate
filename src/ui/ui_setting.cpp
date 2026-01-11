/**
 * @file ui_setting.cpp
 * @brief Setting interface implementation
 */

#include "ui_common.h"
#include "board/TLoRaPagerBoard.h"
#include <stdint.h>
#include <stdio.h>

// Forward declarations from main.cpp
extern uint32_t getScreenSleepTimeout();
extern void setScreenSleepTimeout(uint32_t timeout_ms);
extern uint32_t getGPSCollectionInterval();
extern void setGPSCollectionInterval(uint32_t interval_ms);

static lv_obj_t *menu = NULL;
static lv_group_t *settings_group = NULL;
static lv_obj_t *system_cont = NULL;
static lv_obj_t *gps_cont = NULL;
static lv_obj_t *system_btn = NULL;
static lv_obj_t *gps_btn = NULL;

// System tab widgets
static lv_obj_t *sleep_timeout_btn = NULL;
static lv_obj_t *sleep_timeout_slider = NULL;
static lv_obj_t *sleep_timeout_value_label = NULL;

// GPS tab widgets
static lv_obj_t *gps_interval_btn = NULL;
static lv_obj_t *gps_interval_slider = NULL;
static lv_obj_t *gps_interval_value_label = NULL;

// Editing state
static bool editing_sleep_timeout = false;
static bool editing_gps_interval = false;

static void back_event_handler(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
    if (lv_menu_back_button_is_root(menu, obj)) {
        if (settings_group) {
            lv_group_del(settings_group);
            settings_group = NULL;
        }
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
        system_cont = NULL;
        gps_cont = NULL;
        system_btn = NULL;
        gps_btn = NULL;
        sleep_timeout_btn = NULL;
        sleep_timeout_slider = NULL;
        sleep_timeout_value_label = NULL;
        gps_interval_btn = NULL;
        gps_interval_slider = NULL;
        gps_interval_value_label = NULL;
        editing_sleep_timeout = false;
        editing_gps_interval = false;
        menu_show();
    }
}

/**
 * Format time in seconds to readable string
 */
static void format_time_string(uint32_t seconds, char *buffer, size_t buffer_size)
{
    if (seconds < 60) {
        snprintf(buffer, buffer_size, "%lu 秒", seconds);
    } else {
        uint32_t minutes = seconds / 60;
        uint32_t secs = seconds % 60;
        if (secs == 0) {
            snprintf(buffer, buffer_size, "%lu 分钟", minutes);
        } else {
            snprintf(buffer, buffer_size, "%lu 分 %lu 秒", minutes, secs);
        }
    }
}

/**
 * Update sleep timeout display
 */
static void update_sleep_timeout_display()
{
    if (sleep_timeout_slider && sleep_timeout_value_label) {
        int32_t value = lv_slider_get_value(sleep_timeout_slider);
        char text[32];
        format_time_string(value, text, sizeof(text));
        lv_label_set_text(sleep_timeout_value_label, text);
    }
}

/**
 * Update GPS interval display
 */
static void update_gps_interval_display()
{
    if (gps_interval_slider && gps_interval_value_label) {
        int32_t value = lv_slider_get_value(gps_interval_slider);
        char text[32];
        format_time_string(value, text, sizeof(text));
        lv_label_set_text(gps_interval_value_label, text);
    }
}

/**
 * Sleep timeout button click handler - toggle edit mode
 */
static void sleep_timeout_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (editing_sleep_timeout) {
            // Exit edit mode - hide slider
            editing_sleep_timeout = false;
            if (sleep_timeout_slider) {
                lv_obj_add_flag(sleep_timeout_slider, LV_OBJ_FLAG_HIDDEN);
            }
            if (sleep_timeout_btn) {
                lv_group_focus_obj(sleep_timeout_btn);
            }
        } else {
            // Enter edit mode - show slider
            editing_sleep_timeout = true;
            editing_gps_interval = false;
            if (gps_interval_slider) {
                lv_obj_add_flag(gps_interval_slider, LV_OBJ_FLAG_HIDDEN);
            }
            if (sleep_timeout_slider) {
                lv_obj_clear_flag(sleep_timeout_slider, LV_OBJ_FLAG_HIDDEN);
                lv_group_focus_obj(sleep_timeout_slider);
            }
        }
    }
}

/**
 * Sleep timeout slider event handler
 */
static void sleep_timeout_slider_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        int32_t value = lv_slider_get_value(sleep_timeout_slider);
        uint32_t timeout_ms = value * 1000;  // Convert seconds to milliseconds
        setScreenSleepTimeout(timeout_ms);
        update_sleep_timeout_display();
    }
}

/**
 * GPS interval button click handler - toggle edit mode
 */
static void gps_interval_btn_clicked_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        if (editing_gps_interval) {
            // Exit edit mode - hide slider
            editing_gps_interval = false;
            if (gps_interval_slider) {
                lv_obj_add_flag(gps_interval_slider, LV_OBJ_FLAG_HIDDEN);
            }
            if (gps_interval_btn) {
                lv_group_focus_obj(gps_interval_btn);
            }
        } else {
            // Enter edit mode - show slider
            editing_gps_interval = true;
            editing_sleep_timeout = false;
            if (sleep_timeout_slider) {
                lv_obj_add_flag(sleep_timeout_slider, LV_OBJ_FLAG_HIDDEN);
            }
            if (gps_interval_slider) {
                lv_obj_clear_flag(gps_interval_slider, LV_OBJ_FLAG_HIDDEN);
                lv_group_focus_obj(gps_interval_slider);
            }
        }
    }
}

/**
 * GPS interval slider event handler
 */
static void gps_interval_slider_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        int32_t value = lv_slider_get_value(gps_interval_slider);
        uint32_t interval_ms = value * 1000;  // Convert seconds to milliseconds
        setGPSCollectionInterval(interval_ms);
        update_gps_interval_display();
    }
}

/**
 * Create a setting item with title, description, button and slider
 */
static void create_setting_item(lv_obj_t *parent, const char *title, const char *description,
                                lv_obj_t **btn_out, lv_obj_t **slider_out, lv_obj_t **value_label_out,
                                int32_t min_val, int32_t max_val, int32_t current_val,
                                lv_event_cb_t btn_cb, lv_event_cb_t slider_cb)
{
    // Container for the setting item
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(cont, 15, LV_PART_MAIN);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    
    // Title label
    lv_obj_t *title_label = lv_label_create(cont);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(title_label, 5, LV_PART_MAIN);
    
    // Description label
    lv_obj_t *desc_label = lv_label_create(cont);
    lv_label_set_text(desc_label, description);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(desc_label, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_opa(desc_label, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(desc_label, 10, LV_PART_MAIN);
    
    // Button to enter edit mode (clickable area showing current value)
    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, LV_PCT(100), 50);
    lv_obj_set_style_pad_all(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_color(btn, lv_color_hex(0x00AAFF), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);
    if (btn_out) {
        *btn_out = btn;
    }
    
    // Value label (inside button, shows current value)
    if (value_label_out) {
        *value_label_out = lv_label_create(btn);
        lv_obj_set_style_text_font(*value_label_out, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(*value_label_out, lv_color_white(), LV_PART_MAIN);
        lv_obj_align(*value_label_out, LV_ALIGN_CENTER, 0, 0);
    }
    
    // Slider (initially hidden, shown when editing)
    if (slider_out) {
        *slider_out = lv_slider_create(cont);
        lv_obj_set_width(*slider_out, LV_PCT(95));
        lv_obj_set_style_pad_top(*slider_out, 10, LV_PART_MAIN);
        lv_slider_set_range(*slider_out, min_val, max_val);
        lv_slider_set_value(*slider_out, current_val, LV_ANIM_OFF);
        lv_obj_add_event_cb(*slider_out, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_flag(*slider_out, LV_OBJ_FLAG_HIDDEN);  // Initially hidden
    }
}

static void switch_to_system_tab(lv_event_t *e)
{
    (void)e;
    if (system_cont) {
        lv_obj_clear_flag(system_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (gps_cont) {
        lv_obj_add_flag(gps_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (system_btn) {
        lv_obj_add_state(system_btn, LV_STATE_CHECKED);
    }
    if (gps_btn) {
        lv_obj_clear_state(gps_btn, LV_STATE_CHECKED);
    }
    if (sleep_timeout_btn && settings_group) {
        lv_group_focus_obj(sleep_timeout_btn);
    }
}

static void switch_to_gps_tab(lv_event_t *e)
{
    (void)e;
    if (system_cont) {
        lv_obj_add_flag(system_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (gps_cont) {
        lv_obj_clear_flag(gps_cont, LV_OBJ_FLAG_HIDDEN);
    }
    if (system_btn) {
        lv_obj_clear_state(system_btn, LV_STATE_CHECKED);
    }
    if (gps_btn) {
        lv_obj_add_state(gps_btn, LV_STATE_CHECKED);
    }
    if (gps_interval_btn && settings_group) {
        lv_group_focus_obj(gps_interval_btn);
    }
}

void ui_setting_enter(lv_obj_t *parent)
{
    menu = create_menu(parent, back_event_handler);
    lv_obj_t *main_page = lv_menu_page_create(menu, "设置");

    // Create settings group for rotary navigation
    settings_group = lv_group_create();
    extern lv_group_t *app_g;
    set_default_group(settings_group);
    
    // Create tab button bar (simple buttons instead of tabview)
    lv_obj_t *tab_bar = lv_obj_create(main_page);
    lv_obj_set_size(tab_bar, LV_PCT(100), 30);
    lv_obj_set_style_pad_all(tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_flex_flow(tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    // System tab button
    system_btn = lv_btn_create(tab_bar);
    lv_obj_set_size(system_btn, 100, 25);
    lv_obj_set_style_bg_color(system_btn, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(system_btn, lv_color_hex(0x00AAFF), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(system_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(system_btn, switch_to_system_tab, LV_EVENT_CLICKED, NULL);
    lv_obj_add_state(system_btn, LV_STATE_CHECKED);
    lv_obj_t *system_btn_label = lv_label_create(system_btn);
    lv_label_set_text(system_btn_label, "System");
    lv_obj_center(system_btn_label);
    
    // GPS tab button
    gps_btn = lv_btn_create(tab_bar);
    lv_obj_set_size(gps_btn, 100, 25);
    lv_obj_set_style_bg_color(gps_btn, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(gps_btn, lv_color_hex(0x00AAFF), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(gps_btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(gps_btn, switch_to_gps_tab, LV_EVENT_CLICKED, NULL);
    lv_obj_t *gps_btn_label = lv_label_create(gps_btn);
    lv_label_set_text(gps_btn_label, "GPS");
    lv_obj_center(gps_btn_label);
    
    // Create scrollable container for settings
    lv_obj_t *scroll_cont = lv_obj_create(main_page);
    lv_obj_set_size(scroll_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(scroll_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(scroll_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scroll_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scroll_cont, LV_SCROLLBAR_MODE_OFF);
    
    // Create System settings container
    system_cont = lv_obj_create(scroll_cont);
    lv_obj_set_size(system_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(system_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(system_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(system_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(system_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(system_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    
    // Sleep timeout setting
    create_setting_item(system_cont, 
                       "熄屏时间",
                       "设置屏幕无操作后自动熄屏的时间（10-300秒）",
                       &sleep_timeout_btn, &sleep_timeout_slider, &sleep_timeout_value_label,
                       10, 300, getScreenSleepTimeout() / 1000,
                       sleep_timeout_btn_clicked_cb, sleep_timeout_slider_event_cb);
    
    // Add to group for rotary navigation
    if (sleep_timeout_btn) {
        lv_group_add_obj(settings_group, sleep_timeout_btn);
    }
    if (sleep_timeout_slider) {
        lv_group_add_obj(settings_group, sleep_timeout_slider);
    }
    
    update_sleep_timeout_display();
    
    // Create GPS settings container
    gps_cont = lv_obj_create(scroll_cont);
    lv_obj_set_size(gps_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(gps_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(gps_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gps_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_flex_flow(gps_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(gps_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(gps_cont, LV_OBJ_FLAG_HIDDEN);  // Initially hidden
    
    // GPS collection interval setting
    create_setting_item(gps_cont,
                       "GPS采集频率",
                       "设置GPS数据采集的时间间隔（60-600秒，最快1分钟一次）",
                       &gps_interval_btn, &gps_interval_slider, &gps_interval_value_label,
                       60, 600, getGPSCollectionInterval() / 1000,
                       gps_interval_btn_clicked_cb, gps_interval_slider_event_cb);
    
    // Add to group for rotary navigation
    if (gps_interval_btn) {
        lv_group_add_obj(settings_group, gps_interval_btn);
    }
    if (gps_interval_slider) {
        lv_group_add_obj(settings_group, gps_interval_slider);
    }
    
    update_gps_interval_display();
    
    // Focus first setting button
    if (sleep_timeout_btn) {
        lv_group_focus_obj(sleep_timeout_btn);
    }
    
    lv_menu_set_page(menu, main_page);
}

void ui_setting_exit(lv_obj_t *parent)
{
    (void)parent;
    if (menu) {
        if (settings_group) {
            lv_group_del(settings_group);
            settings_group = NULL;
        }
        lv_obj_clean(menu);
        lv_obj_del(menu);
        menu = NULL;
        system_cont = NULL;
        gps_cont = NULL;
        system_btn = NULL;
        gps_btn = NULL;
        sleep_timeout_btn = NULL;
        sleep_timeout_slider = NULL;
        sleep_timeout_value_label = NULL;
        gps_interval_btn = NULL;
        gps_interval_slider = NULL;
        gps_interval_value_label = NULL;
        editing_sleep_timeout = false;
        editing_gps_interval = false;
    }
}
