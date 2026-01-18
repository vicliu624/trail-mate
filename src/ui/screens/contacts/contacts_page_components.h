/**
 * @file contacts_page_components.h
 * @brief Contacts page UI components (public interface)
 *
 * This header exposes the public UI construction and refresh APIs
 * for the Contacts page. It does not include layout, style, or input
 * details, which are handled internally.
 */

#pragma once

#include "lvgl.h"

/**
 * @brief Create filter panel (first column: Contacts / Nearby)
 *
 * @param parent Parent LVGL object (expected to be a horizontal flex container)
 */
void create_filter_panel(lv_obj_t* parent);

/**
 * @brief Create list panel (second column: contacts list)
 *
 * @param parent Parent LVGL object
 */
void create_list_panel(lv_obj_t* parent);

/**
 * @brief Create action panel (third column: context actions)
 *
 * @param parent Parent LVGL object
 */
void create_action_panel(lv_obj_t* parent);

/**
 * @brief Refresh contacts data from ContactService
 *
 * Note: The actual implementation is located in ui_contacts.cpp
 * to avoid Arduino framework dependency issues.
 */
void refresh_contacts_data();

/**
 * @brief Refresh the Contacts page UI
 *
 * This will:
 * - Rebuild the visible list items for the current page
 * - Update pagination buttons (enable/disable)
 * - Update filter button highlight (Contacts / Nearby)
 *
 * No input behavior is changed by calling this function.
 */
void refresh_ui();

/**
 * @brief Clean up modal windows related to the Contacts page
 *
 * Safely deletes and clears modal object pointers if they exist.
 */
void cleanup_modals();
