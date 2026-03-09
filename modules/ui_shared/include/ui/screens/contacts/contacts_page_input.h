/**
 * @file contacts_page_input.h
 * @brief Contacts page input handling for the shared two-pane navigator
 */

#pragma once

#include "lvgl.h"

void init_contacts_input();
void cleanup_contacts_input();

/**
 * Rebind focusable objects after refresh_ui() rebuilds the current list.
 */
void contacts_input_on_ui_refreshed();

/**
 * Move focus to the filter column or the list column.
 * The filter column contains the visible mode buttons plus the top back button.
 * The list column contains list items, the inline list Back item, and optional pager buttons.
 */
void contacts_focus_to_filter();
void contacts_focus_to_list();

lv_group_t* contacts_input_get_group();
