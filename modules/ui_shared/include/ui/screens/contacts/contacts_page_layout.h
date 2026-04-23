#pragma once

#include "contacts_page_styles.h"
#include "contacts_state.h"
#include "lvgl.h"

namespace contacts
{
namespace ui
{
namespace layout
{

lv_obj_t* create_root(lv_obj_t* parent);
lv_obj_t* create_header(lv_obj_t* root, void (*back_callback)(void*), void* user_data);
void create_footer(lv_obj_t* root);

/**
 * Create the main two-pane content row for the Contacts page.
 */
lv_obj_t* create_content(lv_obj_t* root);

/**
 * Create the selector-side panel for the directory browser scaffold.
 */
void create_filter_panel(lv_obj_t* parent);

/**
 * Create the main content-side panel for the directory browser scaffold.
 */
void create_list_panel(lv_obj_t* parent);

/**
 * Ensure the list subcontainers exist.
 * sub_container holds the list rows; bottom_container is used only when a mode still needs pager buttons.
 */
void ensure_list_subcontainers();

lv_obj_t* create_list_item(lv_obj_t* parent,
                           const chat::contacts::NodeInfo& node,
                           ContactsMode mode,
                           const char* status_text);

} // namespace layout
} // namespace ui
} // namespace contacts
