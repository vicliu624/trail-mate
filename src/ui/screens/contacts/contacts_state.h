/**
 * @file contacts_state.h
 * @brief Contacts page state management
 */

#pragma once

#include "../../../chat/domain/contact_types.h"
#include "../../widgets/top_bar.h"
#include "lvgl.h"
#include <vector>

// Forward declaration
namespace chat
{
namespace contacts
{
class ContactService;
} // namespace contacts
namespace ui
{
class ChatComposeScreen;
class ChatConversationScreen;
} // namespace ui
class ChatService;
} // namespace chat

namespace ui
{
namespace widgets
{
class ImeWidget;
} // namespace widgets
} // namespace ui

namespace contacts
{
namespace ui
{

enum class ContactsMode
{
    Contacts,  // Show contacts (nodes with nicknames)
    Nearby,    // Show nearby nodes (nodes without nicknames)
    Broadcast, // Show broadcast channels
    Team,      // Show team (if joined)
    Discover   // Show MeshCore discover actions
};

struct ContactsPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* page = nullptr; // Content container (second column)

    ::ui::widgets::TopBar top_bar;

    // First column: Filter buttons
    lv_obj_t* filter_panel = nullptr;
    lv_obj_t* contacts_btn = nullptr;
    lv_obj_t* nearby_btn = nullptr;
    lv_obj_t* broadcast_btn = nullptr;
    lv_obj_t* team_btn = nullptr;
    lv_obj_t* discover_btn = nullptr;

    // Second column: Node list
    lv_obj_t* list_panel = nullptr;
    lv_obj_t* sub_container = nullptr;    // Container for list items and pagination
    lv_obj_t* bottom_container = nullptr; // Container for bottom buttons (Prev/Next/Back)
    std::vector<lv_obj_t*> list_items;    // Contact/Node rows
    lv_obj_t* prev_btn = nullptr;
    lv_obj_t* next_btn = nullptr;
    lv_obj_t* back_btn = nullptr; // Return to first column

    // Third column: Action buttons
    lv_obj_t* action_panel = nullptr;
    lv_obj_t* chat_btn = nullptr;
    lv_obj_t* position_btn = nullptr;
    lv_obj_t* edit_btn = nullptr;
    lv_obj_t* del_btn = nullptr;
    lv_obj_t* add_btn = nullptr;
    lv_obj_t* info_btn = nullptr;
    lv_obj_t* action_back_btn = nullptr; // third column back (to list)

    // Current state
    ContactsMode current_mode = ContactsMode::Contacts;
    ContactsMode last_action_mode = ContactsMode::Contacts;
    int selected_index = -1; // Selected item in list
    int current_page = 0;    // Current page (0-based)
    size_t total_items = 0;  // Total items in current mode

    // Data (using forward declaration, full type in .cpp)
    std::vector<chat::contacts::NodeInfo> contacts_list;
    std::vector<chat::contacts::NodeInfo> nearby_list;

    // Timers
    lv_timer_t* refresh_timer = nullptr;

    // Modal windows
    lv_obj_t* add_edit_modal = nullptr;
    lv_obj_t* add_edit_textarea = nullptr;
    lv_obj_t* add_edit_error_label = nullptr;
    lv_obj_t* del_confirm_modal = nullptr;
    lv_obj_t* action_menu_modal = nullptr;
    lv_obj_t* discover_modal = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_group_t* prev_group = nullptr;
    uint32_t modal_node_id = 0;
    bool modal_is_edit = false;
    lv_timer_t* discover_scan_timer = nullptr;
    size_t discover_scan_start_nearby = 0;

    // Compose screen (Chat button)
    chat::ui::ChatComposeScreen* compose_screen = nullptr;
    ::ui::widgets::ImeWidget* compose_ime = nullptr;
    chat::ui::ChatConversationScreen* conversation_screen = nullptr;
    lv_timer_t* conversation_timer = nullptr;

    // Node info screen
    lv_obj_t* node_info_root = nullptr;
    lv_group_t* node_info_group = nullptr;
    lv_group_t* node_info_prev_group = nullptr;

    // Services (owned by AppContext)
    chat::contacts::ContactService* contact_service = nullptr;
    chat::ChatService* chat_service = nullptr;

    bool initialized = false;
    bool exiting = false;
};

extern ContactsPageState g_contacts_state;

} // namespace ui
} // namespace contacts
