/**
 * @file contacts_page_input.h
 * @brief Contacts page input handling (Rotary encoder navigation)
 */

#pragma once

#include "lvgl.h"

/**
 * @brief Initialize input handling (create lv_group, default focus on Filter column)
 */
void init_contacts_input();

/**
 * @brief Clean up input handling (delete lv_group)
 */
void cleanup_contacts_input();

/**
 * @brief MUST be called after refresh_ui() rebuilds list/buttons
 * Rebind lv_group objects based on current focus column.
 */
void contacts_input_on_ui_refreshed();

/**
 * @brief Switch focus column according to Contacts UI spec.
 * - Filter: only Contacts/Nearby buttons
 * - List: list items + Prev/Next/Back
 */
void contacts_focus_to_filter();
void contacts_focus_to_list();
// Kept for compatibility with existing callers; currently maps to list focus.
void contacts_focus_to_action();

/**
 * @brief Get the current input group for Contacts page (may be nullptr if not initialized)
 */
lv_group_t* contacts_input_get_group();
