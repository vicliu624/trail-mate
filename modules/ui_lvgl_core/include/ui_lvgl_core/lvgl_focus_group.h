#pragma once

#include "lvgl.h"

// Transitional LVGL focus-group bridge.
// The implementation still lives in ui_shared/app_runtime.cpp until the
// screen host/runtime shell is migrated out of the ui_shared umbrella.
extern lv_group_t* app_g;

void set_default_group(lv_group_t* group);
