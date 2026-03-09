/**
 * @file contacts_page_components.h
 * @brief Contacts page behavior and refresh API
 */

#pragma once

#include "lvgl.h"

/**
 * Create the filter column UI and bind its focus/click events.
 * The list column structure is created directly via contacts::ui::layout.
 */
void create_filter_panel(lv_obj_t* parent);

void refresh_contacts_data();
void refresh_ui();
void cleanup_modals();
