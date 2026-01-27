#pragma once

#include "lvgl.h"

// Tracker modal and overlay drawing helpers.
void gps_tracker_open_modal();
void gps_tracker_draw_event(lv_event_t* e);
void gps_tracker_cleanup();

