/**
 * @file images.h
 * @brief Image asset declarations for LVGL v9
 * 
 * This header declares external image descriptors that are defined
 * in the corresponding .c files in this directory.
 */

#ifndef UI_ASSETS_IMAGES_H
#define UI_ASSETS_IMAGES_H

#include "lvgl.h"

#if LVGL_VERSION_MAJOR == 9

// Image asset declarations
#ifdef ARDUINO_USB_MODE
extern const lv_image_dsc_t img_usb;
#endif

#endif // LVGL_VERSION_MAJOR == 9

#endif // UI_ASSETS_IMAGES_H
