/**
 * @file node_info_page_components.h
 * @brief Node info page UI components (public interface)
 */

#pragma once

#include "../../../chat/domain/contact_types.h"
#include "lvgl.h"

namespace node_info
{
namespace ui
{

struct NodeInfoWidgets
{
    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* top_row = nullptr;
    lv_obj_t* info_card = nullptr;
    lv_obj_t* info_header = nullptr;
    lv_obj_t* info_footer = nullptr;
    lv_obj_t* location_card = nullptr;
    lv_obj_t* location_header = nullptr;
    lv_obj_t* location_map = nullptr;
    lv_obj_t* location_coords = nullptr;
    lv_obj_t* location_updated = nullptr;
    lv_obj_t* link_panel = nullptr;
    lv_obj_t* link_header = nullptr;
    lv_obj_t* link_row_1 = nullptr;
    lv_obj_t* link_row_2 = nullptr;

    lv_obj_t* back_btn = nullptr;
    lv_obj_t* back_label = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* battery_label = nullptr;

    lv_obj_t* avatar_bg = nullptr;
    lv_obj_t* avatar_label = nullptr;
    lv_obj_t* name_label = nullptr;
    lv_obj_t* desc_label = nullptr;
    lv_obj_t* id_label = nullptr;
    lv_obj_t* role_label = nullptr;

    lv_obj_t* location_title_label = nullptr;
    lv_obj_t* map_image = nullptr;
    lv_obj_t* map_label = nullptr;
    lv_obj_t* coords_latlon_label = nullptr;
    lv_obj_t* coords_acc_label = nullptr;
    lv_obj_t* coords_alt_label = nullptr;
    lv_obj_t* updated_label = nullptr;

    lv_obj_t* link_title_label = nullptr;
    lv_obj_t* link_rssi_label = nullptr;
    lv_obj_t* link_snr_label = nullptr;
    lv_obj_t* link_ch_label = nullptr;
    lv_obj_t* link_sf_label = nullptr;
    lv_obj_t* link_bw_label = nullptr;
    lv_obj_t* link_hop_label = nullptr;
    lv_obj_t* link_last_heard_label = nullptr;
};

/**
 * @brief Create an empty Node Info screen structure.
 */
NodeInfoWidgets create(lv_obj_t* parent);

/**
 * @brief Destroy the Node Info screen (if created).
 */
void destroy();

/**
 * @brief Access last created widgets.
 */
const NodeInfoWidgets& widgets();

/**
 * @brief Update UI labels with NodeInfo data.
 */
void set_node_info(const chat::contacts::NodeInfo& node);

} // namespace ui
} // namespace node_info
