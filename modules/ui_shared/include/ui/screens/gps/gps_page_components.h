#ifndef GPS_PAGE_COMPONENTS_H
#define GPS_PAGE_COMPONENTS_H

#include "lvgl.h"

void show_loading();
void hide_loading();

void show_toast(const char* message, uint32_t duration_ms = 2000);
void hide_toast();

void show_pan_h_indicator();
void hide_pan_h_indicator();
void show_pan_v_indicator();
void hide_pan_v_indicator();

void show_zoom_popup();
void hide_zoom_popup();

void show_layer_popup();
void hide_layer_popup();

void show_route_popup();
void hide_route_popup();

void fix_ui_elements_position();

#endif // GPS_PAGE_COMPONENTS_H
