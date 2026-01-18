#pragma once

#include "lvgl.h"
#include "contacts_state.h"              // g_contacts_state, ContactsMode
#include "contacts_page_styles.h"        // style::apply_*

namespace contacts {
namespace ui {
namespace layout {

/**
 * @brief Create root container for contacts page
 * @param parent Parent object (usually main screen)
 * @return Created root container
 */
lv_obj_t* create_root(lv_obj_t* parent);

/**
 * @brief Create header container with top bar
 * @param root Root container
 * @param back_callback Callback for back button
 * @param user_data User data for callback
 * @return Created header container
 */
lv_obj_t* create_header(lv_obj_t* root,
                        void (*back_callback)(void*),
                        void* user_data);

/**
 * @brief Create content container (three columns parent)
 * @param root Root container
 * @return Created content container
 */
lv_obj_t* create_content(lv_obj_t* root);

/**
 * @brief Create filter panel (first column)
 */
void create_filter_panel(lv_obj_t* parent);

/**
 * @brief Create list panel (second column)
 */
void create_list_panel(lv_obj_t* parent);

/**
 * @brief Create action panel (third column)
 */
void create_action_panel(lv_obj_t* parent);

/**
 * @brief Ensure list sub containers exist (sub_container + bottom_container)
 * Called from refresh_ui() before creating list items / buttons.
 */
void ensure_list_subcontainers();

/**
 * @brief Create one list item (row) under parent, and push into g_contacts_state.list_items.
 * @return the created item object.
 */
lv_obj_t* create_list_item(lv_obj_t* parent,
                           const chat::contacts::NodeInfo& node,
                           ContactsMode mode,
                           const char* status_text /* already formatted */);

} // namespace layout
} // namespace ui
} // namespace contacts
