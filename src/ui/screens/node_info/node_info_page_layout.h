/**
 * @file node_info_page_layout.h
 * @brief Node info page layout (structure only)
 */

#pragma once

#include "lvgl.h"

namespace node_info
{
namespace ui
{
namespace layout
{

lv_obj_t* create_root(lv_obj_t* parent);
lv_obj_t* create_header(lv_obj_t* root);
lv_obj_t* create_content(lv_obj_t* root);
lv_obj_t* create_top_row(lv_obj_t* content);
lv_obj_t* create_info_card(lv_obj_t* top_row);
lv_obj_t* create_info_header(lv_obj_t* info_card);
lv_obj_t* create_info_footer(lv_obj_t* info_card);
lv_obj_t* create_location_card(lv_obj_t* top_row);
lv_obj_t* create_location_header(lv_obj_t* location_card);
lv_obj_t* create_location_map(lv_obj_t* location_card);
lv_obj_t* create_location_coords(lv_obj_t* location_card);
lv_obj_t* create_location_updated(lv_obj_t* location_card);
lv_obj_t* create_link_panel(lv_obj_t* content);
lv_obj_t* create_link_header(lv_obj_t* link_panel);
lv_obj_t* create_link_row(lv_obj_t* link_panel);

} // namespace layout
} // namespace ui
} // namespace node_info
