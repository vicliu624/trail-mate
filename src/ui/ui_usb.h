/**
 * @file ui_usb.h
 * @brief USB Mass Storage interface header
 */

#ifndef UI_USB_H
#define UI_USB_H

#include "lvgl.h"

#ifdef ARDUINO_USB_MODE

/**
 * @brief Enter USB Mass Storage mode
 * @param parent Parent object for the UI
 */
void ui_usb_enter(lv_obj_t* parent);

/**
 * @brief Exit USB Mass Storage mode
 * @param parent Parent object for the UI
 */
void ui_usb_exit(lv_obj_t* parent);

/**
 * @brief Check if USB Mass Storage mode is currently active
 * @return true if USB mode is active, false otherwise
 */
bool ui_usb_is_active();

#endif // ARDUINO_USB_MODE

#endif // UI_USB_H
