/**
 * @file contacts_page_input.h
 * @brief Contacts page input handling for the shared directory browser interaction
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
 * Move focus to the selector region or the content region.
 * The selector region contains the visible mode buttons plus the top back button.
 * The content region contains list items, the inline list Back item, and optional pager buttons.
 */
void contacts_focus_to_selector();
void contacts_focus_to_content();

lv_group_t* contacts_input_get_group();
