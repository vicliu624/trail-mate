/**
 * @file top_bar.h
 * @brief Shared top bar widget (back button + centered title + right status)
 */

#pragma once

#include "lvgl.h"
#include <cstdint>

namespace ui {
namespace widgets {

struct TopBarConfig {
    lv_obj_t* back_btn_override = nullptr;      // Use existing back button (keeps its style)
    lv_obj_t* title_label_override = nullptr;   // Use existing title label if provided
    bool create_back = true;                    // Create new back button when no override
    lv_coord_t height = 26;                     // Default height
};

struct TopBar {
    lv_obj_t* container = nullptr;
    lv_obj_t* back_btn = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* right_label = nullptr;
    void (*back_cb)(void*) = nullptr;
    void* back_user_data = nullptr;
};

constexpr uint16_t kTopBarHeight = 30;

/**
 * @brief Initialize a top bar on the given parent
 */
void top_bar_init(TopBar& bar, lv_obj_t* parent, const TopBarConfig& config = {});

/**
 * @brief Update title text (center label)
 */
void top_bar_set_title(TopBar& bar, const char* title);

/**
 * @brief Update right-side text (status/battery/etc)
 */
void top_bar_set_right_text(TopBar& bar, const char* text);

/**
 * @brief Set back button callback
 */
void top_bar_set_back_callback(TopBar& bar, void (*cb)(void*), void* user_data);

} // namespace widgets
} // namespace ui
