#pragma once

#include "lvgl.h"

// Compatibility entrypoint used by shared touch editors. Linux discovers the
// input registered by its host; no ESP display helper is linked here.
inline lv_indev_t* lv_get_keyboard_indev()
{
    for (lv_indev_t* input = lv_indev_get_next(nullptr); input;
         input = lv_indev_get_next(input))
    {
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_KEYPAD) return input;
    }
    return nullptr;
}
