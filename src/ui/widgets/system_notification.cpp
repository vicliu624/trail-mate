/**
 * @file system_notification.cpp
 * @brief System-level notification toast component implementation
 */

#include "system_notification.h"
#include <cstring>

namespace ui
{

lv_obj_t* SystemNotification::container_ = nullptr;
lv_obj_t* SystemNotification::icon_ = nullptr;
lv_obj_t* SystemNotification::label_ = nullptr;
lv_timer_t* SystemNotification::hide_timer_ = nullptr;
bool SystemNotification::visible_ = false;

extern "C"
{
    extern const lv_image_dsc_t alert;
}

void SystemNotification::init()
{
    if (container_)
    {
        return; // Already initialized
    }

    // Get the active screen (top layer)
    lv_obj_t* top_layer = lv_layer_top();

    // Create container - with 30px margins on left and right
    container_ = lv_obj_create(top_layer);
    // Get screen width and calculate container width (screen width - 60px for 30px margins on each side)
    lv_coord_t screen_width = lv_display_get_physical_horizontal_resolution(NULL);
    lv_coord_t container_width = screen_width - 60; // 30px left + 30px right margin
    lv_obj_set_size(container_, container_width, 50);
    lv_obj_set_pos(container_, 30, -60); // x=30 for 30px left margin, will animate to y=0
    lv_obj_set_style_bg_color(container_, lv_color_hex(0xFFF0D3), 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_COVER, 0);
    // Set radius for bottom corners only
    // Since LVGL doesn't support per-corner radius, we'll set a larger radius (20)
    // and position the container at y=0 so top corners are effectively square (clipped by screen edge)
    lv_obj_set_style_radius(container_, 20, 0); // Larger radius for bottom corners (increased from 8 to 20)
    lv_obj_set_style_pad_all(container_, 8, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_shadow_width(container_, 10, 0);
    lv_obj_set_style_shadow_color(container_, lv_color_hex(0xD9B06A), 0);
    lv_obj_set_style_shadow_opa(container_, LV_OPA_50, 0);

    // Create flex layout
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(container_, 8, 0);

    // Create icon
    icon_ = lv_image_create(container_);
    lv_image_set_src(icon_, &alert);
    lv_obj_set_style_width(icon_, 24, 0);
    lv_obj_set_style_height(icon_, 24, 0);

    // Create label with larger font
    label_ = lv_label_create(container_);
    lv_label_set_text(label_, "");
    lv_obj_set_style_text_color(label_, lv_color_hex(0x3A2A1A), 0);
    lv_obj_set_style_text_font(label_, &lv_font_montserrat_18, 0); // Larger font (was 14, now 18)
    lv_obj_set_flex_grow(label_, 1);

    // Initially hidden
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void SystemNotification::show(const char* text, uint32_t duration_ms)
{
    if (!container_)
    {
        init();
    }

    // Truncate text to 15 characters
    char truncated[16];
    size_t len = strlen(text);
    if (len > 15)
    {
        strncpy(truncated, text, 15);
        truncated[15] = '\0';
    }
    else
    {
        strcpy(truncated, text);
    }

    lv_label_set_text(label_, truncated);

    // Cancel existing timer if any
    if (hide_timer_)
    {
        lv_timer_del(hide_timer_);
        hide_timer_ = nullptr;
    }

    // If already visible, hide first
    if (visible_)
    {
        hide();
    }

    // Set initial position (above screen)
    lv_obj_set_y(container_, -60);

    // Show container
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_HIDDEN);
    visible_ = true;

    // Animate from top (y = -60) to visible position (y = 0, tight to top edge)
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container_);
    lv_anim_set_values(&anim, -60, 0); // Animate to y=0 (top edge, no margin)
    lv_anim_set_time(&anim, 300);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_ready_cb(&anim, animReadyCallback);
    lv_anim_start(&anim);

    // Set up auto-hide timer
    hide_timer_ = lv_timer_create(hideTimerCallback, duration_ms, nullptr);
    lv_timer_set_repeat_count(hide_timer_, 1);
}

void SystemNotification::hide()
{
    if (!container_ || !visible_)
    {
        return;
    }

    // Cancel timer
    if (hide_timer_)
    {
        lv_timer_del(hide_timer_);
        hide_timer_ = nullptr;
    }

    // Animate out (move to y = -60)
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, container_);
    lv_anim_set_values(&anim, lv_obj_get_y(container_), -60);
    lv_anim_set_time(&anim, 300);
    lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_ready_cb(&anim, [](lv_anim_t* a)
                         {
        (void)a;
        lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);
        visible_ = false; });
    lv_anim_start(&anim);
}

bool SystemNotification::isVisible()
{
    return visible_ && container_ && !lv_obj_has_flag(container_, LV_OBJ_FLAG_HIDDEN);
}

void SystemNotification::hideTimerCallback(lv_timer_t* timer)
{
    (void)timer;
    hide();
}

void SystemNotification::animReadyCallback(lv_anim_t* anim)
{
    (void)anim;
    // Animation completed, notification is now visible
}

} // namespace ui
