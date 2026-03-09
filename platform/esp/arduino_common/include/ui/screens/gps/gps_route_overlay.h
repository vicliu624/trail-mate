#pragma once

#include "lvgl.h"

// Route (KML) overlay helpers.
bool gps_route_sync_from_config(bool show_toast);
bool gps_route_focus(bool show_toast);
void gps_route_draw_event(lv_event_t* e);
void gps_route_cleanup();
