#pragma once

#include "lvgl.h"

// Tracker modal and overlay drawing helpers.
void gps_tracker_open_modal();
bool gps_tracker_load_file(const char* path, bool show_toast);
void gps_tracker_draw_event(lv_event_t* e);
void gps_tracker_cleanup();
