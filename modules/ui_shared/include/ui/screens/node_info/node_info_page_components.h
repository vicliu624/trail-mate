/**
 * @file node_info_page_components.h
 * @brief Node info page UI components (public interface)
 */

#pragma once

#include <cstddef>

#include "chat/domain/contact_types.h"
#include "lvgl.h"
#include "ui/widgets/map/map_viewport.h"

namespace node_info
{
namespace ui
{

static constexpr std::size_t kNodeInfoInfoLineCount = 4;

struct NodeInfoWidgets
{
    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;

    lv_obj_t* back_btn = nullptr;
    lv_obj_t* back_label = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* battery_label = nullptr;

    lv_obj_t* map_stage = nullptr;
    lv_obj_t* tile_layer = nullptr;
    lv_obj_t* map_overlay_layer = nullptr;
    lv_obj_t* node_overlay_layer = nullptr;
    lv_obj_t* map_gesture_surface = nullptr;

    lv_obj_t* id_label = nullptr;
    lv_obj_t* lon_label = nullptr;
    lv_obj_t* lat_label = nullptr;
    lv_obj_t* no_position_label = nullptr;

    lv_obj_t* connection_line = nullptr;
    lv_obj_t* marker_node_ring = nullptr;
    lv_obj_t* marker_node_dot = nullptr;
    lv_obj_t* marker_self_ring = nullptr;
    lv_obj_t* marker_self_dot = nullptr;
    lv_obj_t* distance_label = nullptr;

    lv_obj_t* zoom_in_btn = nullptr;
    lv_obj_t* zoom_out_btn = nullptr;
    lv_obj_t* zoom_in_label = nullptr;
    lv_obj_t* zoom_out_label = nullptr;
    lv_obj_t* info_panel = nullptr;
    lv_obj_t* zoom_status_label = nullptr;
    lv_obj_t* layer_btn = nullptr;
    lv_obj_t* layer_label = nullptr;

    lv_obj_t* info_labels[kNodeInfoInfoLineCount]{};
    ::ui::widgets::map::Widgets map_viewport{};
};

/**
 * @brief Create the Node Info page widgets.
 */
NodeInfoWidgets create(lv_obj_t* parent);

/**
 * @brief Destroy the Node Info page (if created).
 */
void destroy();

/**
 * @brief Access last created widgets.
 */
const NodeInfoWidgets& widgets();

/**
 * @brief Update UI widgets with NodeInfo data.
 */
void set_node_info(const chat::contacts::NodeInfo& node);

} // namespace ui
} // namespace node_info
