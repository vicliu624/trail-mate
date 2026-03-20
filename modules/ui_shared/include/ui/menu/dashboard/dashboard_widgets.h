#pragma once

#include "lvgl.h"

namespace ui::menu::dashboard
{

void create_mesh_widget(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h);
void refresh_mesh_widget();

void create_gps_widget(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h);
void refresh_gps_widget();

void create_recent_widget(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h);
void refresh_recent_widget();

void create_compass_widget(lv_obj_t* parent, lv_coord_t card_w, lv_coord_t card_h);
void refresh_compass_widget();

} // namespace ui::menu::dashboard
