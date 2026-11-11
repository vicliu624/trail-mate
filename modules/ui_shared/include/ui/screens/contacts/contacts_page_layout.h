#pragma once

#include "contacts_page_styles.h"
#include "contacts_state.h"
#include "lvgl.h"

#include <string>

namespace contacts
{
namespace ui
{
namespace layout
{

lv_obj_t* create_root(lv_obj_t* parent);
lv_obj_t* create_header(lv_obj_t* root, void (*back_callback)(void*), void* user_data);

/**
 * Create the main two-pane content row for the Contacts page.
 */
lv_obj_t* create_content(lv_obj_t* root);

/**
 * Create the left filter column.
 */
void create_filter_panel(lv_obj_t* parent);

/**
 * Create the main list column.
 */
void create_list_panel(lv_obj_t* parent);

/**
 * Ensure the list subcontainers exist.
 * sub_container holds the list rows; page actions use the root footer.
 */
void ensure_list_subcontainers();

std::string preferred_node_display_name(const chat::contacts::PeerDirectoryItem& node);

lv_obj_t* create_list_item(lv_obj_t* parent,
                           const chat::contacts::PeerDirectoryItem& node,
                           ContactsMode mode,
                           const char* status_text);

} // namespace layout
} // namespace ui
} // namespace contacts
