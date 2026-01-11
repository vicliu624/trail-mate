/**
 * @file gps_page.h
 * @brief GPS page UI and page-level scheduling
 */

#ifndef GPS_PAGE_H
#define GPS_PAGE_H

#include "lvgl.h"

// ============================================================================
// Configuration Constants
// ============================================================================

#define MAP_PAN_STEP 32
#define DEFAULT_ZOOM 12
#define MIN_ZOOM 0
#define MAX_ZOOM 18

// Default location when no GPS data available (London, UK)
#define DEFAULT_LAT 51.5074
#define DEFAULT_LNG -0.1278

// ============================================================================
// Public API
// ============================================================================

// Page lifecycle
void ui_gps_enter(lv_obj_t *parent);
void ui_gps_exit(lv_obj_t *parent);

// Global function to check if tiles are loading (for LV_Helper)
bool isGPSLoadingTiles();

// Input helper function
bool indev_is_pressed(lv_event_t *e);

#endif // GPS_PAGE_H
