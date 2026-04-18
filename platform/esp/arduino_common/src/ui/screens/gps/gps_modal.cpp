/**
 * @file gps_modal.cpp
 * @brief Modal framework implementation
 */

#include "ui/screens/gps/gps_modal.h"
#include "sys/clock.h"
#include "ui/LV_Helper.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/screens/gps/gps_page_lifetime.h"
#include "ui/screens/gps/gps_page_styles.h"
#include "ui/ui_common.h"
#include <cstdio>

// Configuration
#define POPUP_DEBOUNCE_MS 300

// Debug logging control
#define GPS_DEBUG 0
#if GPS_DEBUG
#define GPS_LOG(...) std::printf(__VA_ARGS__)
#else
#define GPS_LOG(...)
#endif

using gps::ui::lifetime::is_alive;

void bind_navigation_inputs_to_group(lv_group_t* g)
{
    lv_indev_t* indev = nullptr;
    for (;;)
    {
        indev = lv_indev_get_next(indev);
        if (indev == nullptr)
        {
            break;
        }

        const lv_indev_type_t type = lv_indev_get_type(indev);
        if (type == LV_INDEV_TYPE_ENCODER || type == LV_INDEV_TYPE_KEYPAD)
        {
            lv_indev_set_group(indev, g);
        }
    }
}

bool modal_uses_touch_layout()
{
    return ::ui::page_profile::current().large_touch_hitbox;
}

lv_coord_t modal_resolve_title_height()
{
    const lv_coord_t base = ::ui::page_profile::resolve_popup_title_height();
    if (modal_uses_touch_layout())
    {
        return base > 60 ? base : 60;
    }
    return base > 35 ? base : 35;
}

lv_coord_t modal_resolve_action_button_height()
{
    const lv_coord_t base = ::ui::page_profile::resolve_control_button_height();
    if (modal_uses_touch_layout())
    {
        return base > 64 ? base : 64;
    }
    return base;
}

lv_obj_t* modal_create_touch_title_bar(lv_obj_t* win, const char* title)
{
    if (!win)
    {
        return nullptr;
    }

    lv_obj_t* title_bar = lv_obj_create(win);
    lv_obj_set_size(title_bar, LV_PCT(100), modal_resolve_title_height());
    gps::ui::styles::apply_zoom_popup_title_bar(title_bar);
    lv_obj_align(title_bar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* title_label = lv_label_create(title_bar);
    ::ui::i18n::set_label_text(title_label, title ? title : "");
    gps::ui::styles::apply_zoom_popup_title_label(title_label);
    lv_obj_center(title_label);
    return title_bar;
}

lv_obj_t* modal_create_touch_content_area(lv_obj_t* win, lv_coord_t title_height)
{
    if (!win)
    {
        return nullptr;
    }

    lv_obj_t* content_area = lv_obj_create(win);
    const lv_coord_t win_height = lv_obj_get_height(win);
    const lv_coord_t pad_top = lv_obj_get_style_pad_top(win, LV_PART_MAIN);
    const lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(win, LV_PART_MAIN);
    const lv_coord_t inner_height = win_height > (pad_top + pad_bottom)
                                        ? static_cast<lv_coord_t>(win_height - pad_top - pad_bottom)
                                        : 0;
    const lv_coord_t content_height = inner_height > title_height ? (inner_height - title_height) : 0;
    lv_obj_set_size(content_area, LV_PCT(100), content_height);
    gps::ui::styles::apply_zoom_popup_content_area(content_area);
    lv_obj_align(content_area, LV_ALIGN_BOTTOM_MID, 0, 0);

    const lv_coord_t modal_pad = ::ui::page_profile::resolve_modal_pad();
    lv_obj_set_style_pad_all(content_area, modal_pad, 0);
    lv_obj_set_style_pad_row(content_area, modal_pad, 0);
    lv_obj_clear_flag(content_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(content_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content_area, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return content_area;
}

lv_obj_t* modal_create_touch_action_button(lv_obj_t* parent,
                                           const char* text,
                                           lv_event_cb_t cb,
                                           void* user_data,
                                           lv_coord_t width)
{
    if (!parent)
    {
        return nullptr;
    }

    lv_obj_t* button = lv_btn_create(parent);
    gps::ui::styles::apply_control_button(button);
    lv_obj_set_height(button, modal_resolve_action_button_height());
    lv_obj_set_width(button, width);
    if (cb != nullptr)
    {
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);
    }

    lv_obj_t* label = lv_label_create(button);
    ::ui::i18n::set_label_text(label, text ? text : "");
    gps::ui::styles::apply_control_button_label(label);
    lv_obj_center(label);
    return button;
}

/**
 * Open a modal popup with background overlay
 */
bool modal_open(Modal& m, lv_obj_t* content_root, lv_group_t* focus_group)
{
    if (!is_alive() && !content_root)
    {
        return false;
    }
    if (m.is_open())
    {
        GPS_LOG("[GPS] Modal already open\n");
        return false;
    }

    // Debounce check
    uint32_t now = sys::millis_now();
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
    const auto modal_size = ::ui::page_profile::resolve_modal_size(250, 150, screen);
    lv_coord_t win_x = (screen_w - modal_size.width) / 2;
    lv_coord_t win_y = (screen_h - modal_size.height) / 2;
    lv_obj_set_size(m.win, modal_size.width, modal_size.height);
    lv_obj_set_pos(m.win, win_x, win_y);

    gps::ui::styles::apply_modal_win(m.win);

    lv_obj_move_to_index(m.win, -1);

    // Setup group management
    // Get current default group (via lv_group_get_default) instead of assuming app_g
    m.prev_default = lv_group_get_default() ? lv_group_get_default() : app_g;

    if (m.group == NULL)
    {
        m.group = lv_group_create();
    }
    // Clear group - caller (show_zoom_popup) will add popup_label and configure everything
    lv_group_remove_all_objs(m.group);
    // Don't bind navigation inputs here - let the caller do it after focus
    // targets are added to the modal group.
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
        // Restore default group and navigation-input binding BEFORE deleting objects
        lv_group_t* restore = m.prev_default ? m.prev_default : app_g;
        set_default_group(restore);
        bind_navigation_inputs_to_group(restore);
    }

    // Only delete bg (win is child of bg, will be deleted automatically)
    if (m.bg != NULL)
    {
        lv_obj_del(m.bg);
        m.bg = nullptr;
        m.win = nullptr; // Clear pointer (object already deleted)
    }

    m.close_ms = sys::millis_now();
    m.open = false;
    GPS_LOG("[GPS] Modal closed\n");
}

bool modal_is_open(const Modal& m)
{
    return m.open;
}

void modal_set_size(Modal& m, lv_coord_t w, lv_coord_t h)
{
    if (!m.is_open() || !m.win)
    {
        return;
    }
    lv_obj_set_size(m.win, w, h);
    lv_obj_align(m.win, LV_ALIGN_CENTER, 0, 0);
}

void modal_set_requested_size(Modal& m, lv_coord_t requested_w, lv_coord_t requested_h)
{
    if (!m.is_open() || !m.win)
    {
        return;
    }

    const auto modal_size = ::ui::page_profile::resolve_modal_size(requested_w, requested_h, m.bg ? m.bg : lv_screen_active());
    lv_obj_set_size(m.win, modal_size.width, modal_size.height);
    lv_obj_align(m.win, LV_ALIGN_CENTER, 0, 0);
}
