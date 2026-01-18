#pragma once

#include "../display/DisplayInterface.h"
#include <Arduino.h>
#include <lvgl.h>

#if LV_USE_FS_POSIX != 1 || LV_FS_POSIX_LETTER != 'A'
#warning "Lvgl fs mismatch, may not be able to use fs function"
#endif

void beginLvglHelper(LilyGo_Display &display, bool debug = false);
void updateLvglHelper();

void lv_set_default_group(lv_group_t *group);
lv_indev_t *lv_get_touch_indev();
lv_indev_t *lv_get_keyboard_indev();
lv_indev_t *lv_get_encoder_indev();
