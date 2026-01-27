#ifndef GPS_PAGE_COMPONENTS_H
#define GPS_PAGE_COMPONENTS_H

#include "lvgl.h"

// Loading component
void show_loading();
void hide_loading();

// Toast component
void show_toast(const char* message, uint32_t duration_ms = 2000);
void hide_toast();

// PanIndicator component
void show_pan_h_indicator();
void hide_pan_h_indicator();
void show_pan_v_indicator();
void hide_pan_v_indicator();

// ZoomModal component
void show_zoom_popup();
void hide_zoom_popup();

// UI layout helper
void fix_ui_elements_position();

#endif // GPS_PAGE_COMPONENTS_H
