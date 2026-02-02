/**
 * @file gps_modal.cpp
 * @brief Modal framework implementation
 */

#include "gps_modal.h"
#include "../../LV_Helper.h"
#include "../../ui_common.h"
#include "gps_page_lifetime.h"
#include "gps_page_styles.h"
#include <Arduino.h>

// Configuration
#define POPUP_DEBOUNCE_MS 300

// Debug logging control
#define GPS_DEBUG 0
#if GPS_DEBUG
#define GPS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

// Static encoder cache
static lv_indev_t* g_encoder_indev = nullptr;

using gps::ui::lifetime::is_alive;

/**
 * Get encoder input device (use helper function from LV_Helper)
 */
lv_indev_t* get_encoder_indev()
{
    if (g_encoder_indev != NULL)
    {
        return g_encoder_indev;
    }

    // Use helper function from LV_Helper.h
    g_encoder_indev = lv_get_encoder_indev();
    if (g_encoder_indev == NULL)
    {
        GPS_LOG("[GPS] WARNING: encoder indev is NULL - popup may not receive input\n");
    }
    return g_encoder_indev;
}

/**
 * Bind encoder input device to a group
 */
void bind_encoder_to_group(lv_group_t* g)
{
    lv_indev_t* encoder = get_encoder_indev();
    if (encoder)
    {
        lv_indev_set_group(encoder, g);
    }
}

/**
 * Open a modal popup with background overlay
 */
bool modal_open(Modal& m, lv_obj_t* content_root, lv_group_t* focus_group)
{
    if (!is_alive())
    {
        return false;
    }
    if (m.is_open())
    {
        GPS_LOG("[GPS] Modal already open\n");
        return false;
    }

    // Debounce check
    uint32_t now = millis();
    if (m.close_ms > 0 && (now - m.close_ms) < POPUP_DEBOUNCE_MS)
    {
        GPS_LOG("[GPS] Ignoring modal open request (debounce: %dms since close)\n", now - m.close_ms);
        return false;
    }

    (void)focus_group;
    lv_obj_t* screen = content_root ? content_root : lv_screen_active();
    if (screen == NULL)
    {
        GPS_LOG("[GPS] ERROR: screen is NULL, cannot create modal\n");
        return false;
    }

    // Create modal background
    m.bg = lv_obj_create(screen);
    if (m.bg == NULL)
    {
        GPS_LOG("[GPS] ERROR: Failed to create modal background\n");
        return false;
    }

    lv_coord_t screen_w = lv_obj_get_width(screen);
    lv_coord_t screen_h = lv_obj_get_height(screen);
    lv_obj_set_size(m.bg, screen_w, screen_h);
    lv_obj_set_pos(m.bg, 0, 0);
    gps::ui::styles::apply_modal_bg(m.bg);
    lv_obj_move_to_index(m.bg, -1);

    // Modal bg only swallows pointer events, not KEY/ROTARY
    lv_obj_add_event_cb(
        m.bg, [](lv_event_t* e)
        {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CLICKED || code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED) {
            lv_event_stop_bubbling(e);
        } },
        LV_EVENT_ALL, NULL);

    // Create modal window
    m.win = lv_obj_create(m.bg);
    if (m.win == NULL)
    {
        GPS_LOG("[GPS] ERROR: Failed to create modal window\n");
        lv_obj_del(m.bg);
        m.bg = nullptr;
        return false;
    }

    // Center modal window
    lv_coord_t win_w = 250;
    lv_coord_t win_h = 150;
    lv_coord_t win_x = (screen_w - win_w) / 2;
    lv_coord_t win_y = (screen_h - win_h) / 2;
    lv_obj_set_size(m.win, win_w, win_h);
    lv_obj_set_pos(m.win, win_x, win_y);

    gps::ui::styles::apply_modal_win(m.win);

    lv_obj_move_to_index(m.win, -1);

    // Setup group management
    extern lv_group_t* app_g;
    // Get current default group (via lv_group_get_default) instead of assuming app_g
    m.prev_default = lv_group_get_default() ? lv_group_get_default() : app_g;

    if (m.group == NULL)
    {
        m.group = lv_group_create();
    }
    // Clear group - caller (show_zoom_popup) will add popup_label and configure everything
    lv_group_remove_all_objs(m.group);
    // Don't bind encoder here - let show_zoom_popup do it after popup_label is added and focused
    GPS_LOG("[GPS] Modal group created (empty), group=%p\n", m.group);

    m.close_ms = 0; // Reset close timestamp
    m.open = true;

    GPS_LOG("[GPS] Modal opened successfully\n");
    return true;
}

/**
 * Close modal popup and restore previous state
 */
void modal_close(Modal& m)
{
    if (!m.is_open())
    {
        return;
    }

    if (is_alive())
    {
        // Restore default group and encoder binding BEFORE deleting objects
        extern lv_group_t* app_g;
        lv_group_t* restore = m.prev_default ? m.prev_default : app_g;
        set_default_group(restore);
        bind_encoder_to_group(restore);
    }

    // Only delete bg (win is child of bg, will be deleted automatically)
    if (m.bg != NULL)
    {
        lv_obj_del(m.bg);
        m.bg = nullptr;
        m.win = nullptr; // Clear pointer (object already deleted)
    }

    m.close_ms = millis();
    m.open = false;
    GPS_LOG("[GPS] Modal closed\n");
}

bool modal_is_open(const Modal& m)
{
    return m.open;
}
