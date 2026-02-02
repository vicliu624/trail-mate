/**
 * @file system_notification.h
 * @brief System-level notification toast component
 */

#pragma once

#include "lvgl.h"

namespace ui
{

/**
 * @brief System-level notification toast
 * Displays a notification bubble at the top of the screen
 */
class SystemNotification
{
  public:
    /**
     * @brief Initialize the notification system
     * Must be called after LVGL is initialized
     */
    static void init();

    /**
     * @brief Show a notification
     * @param text Message text (will be truncated to 15 characters)
     * @param duration_ms Display duration in milliseconds (default 3000ms)
     */
    static void show(const char* text, uint32_t duration_ms = 3000);

    /**
     * @brief Hide the current notification
     */
    static void hide();

    /**
     * @brief Check if notification is currently visible
     */
    static bool isVisible();

  private:
    static void hideTimerCallback(lv_timer_t* timer);
    static void animReadyCallback(lv_anim_t* anim);

    static lv_obj_t* container_;
    static lv_obj_t* icon_;
    static lv_obj_t* label_;
    static lv_timer_t* hide_timer_;
    static bool visible_;
};

} // namespace ui
