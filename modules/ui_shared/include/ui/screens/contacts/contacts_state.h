/**
 * @file contacts_state.h
 * @brief Contacts page state management
 */

#pragma once

#include "chat/domain/contact_types.h"
#include "chat/domain/peer_browse_entry.h"
#include "lvgl.h"
#include "platform/ui/map_search_runtime.h"
#include "ui/components/floating_search_box.h"
#include "ui/widgets/top_bar.h"
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
class ChatConversationScreen;
} // namespace ui
class ChatService;
} // namespace chat

namespace contacts
{
namespace ui
{

enum class ContactsMode
{
    Contacts,  // Show contacts (nodes with nicknames)
    Nearby,    // Show nearby nodes (nodes without nicknames)
    Groups,    // Show Reticulum group destinations
    Ignored,   // Show ignored nodes so they can be managed/unignored
    Broadcast, // Show broadcast channels
    Team,      // Show team (if joined)
    Discover,  // Show MeshCore discover actions
    Public     // Show Reticulum plain/public destinations
};

struct ContactsPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* page = nullptr; // Main content row container

    ::ui::widgets::TopBar top_bar;

    // First column: Filter buttons
    lv_obj_t* filter_panel = nullptr;
    lv_obj_t* contacts_btn = nullptr;
    lv_obj_t* nearby_btn = nullptr;
    lv_obj_t* groups_btn = nullptr;
    lv_obj_t* ignored_btn = nullptr;
    lv_obj_t* broadcast_btn = nullptr;
    lv_obj_t* team_btn = nullptr;
    lv_obj_t* discover_btn = nullptr;

    // Main list column
    lv_obj_t* list_panel = nullptr;
    lv_obj_t* sub_container = nullptr;    // Scrollable list content container
    lv_obj_t* bottom_container = nullptr; // Shared page footer, including Menu
    std::vector<lv_obj_t*> list_items;    // Currently visible list rows
    lv_obj_t* empty_label = nullptr;      // Non-focusable empty/search result hint
    lv_obj_t* prev_btn = nullptr;         // Optional pager button
    lv_obj_t* next_btn = nullptr;         // Optional pager button
    lv_obj_t* back_btn = nullptr;         // Bottom-row back button for non-scroll modes
    lv_obj_t* card_details_btn = nullptr; // Node-card action in the shared footer

    // Current state
    ContactsMode current_mode = ContactsMode::Contacts;
    ContactsMode last_action_mode = ContactsMode::Contacts;
    int selected_index = -1; // Selected item in list
    int current_page = 0;    // Current page (0-based)
    size_t total_items = 0;  // Total items in current mode
    bool rendered_mode_valid = false;
    ContactsMode rendered_mode = ContactsMode::Contacts;
    char rendered_search_query[32] = {};
    bool filter_panel_visible = true;
    uint32_t contacts_data_signature = 0;
    uint32_t contacts_data_revision = 0;
    uint32_t rendered_data_revision = 0;
    bool contacts_data_signature_valid = false;
    // True until the platform finishes the initial durable peer-directory hydration.
    // This keeps an empty startup cache distinct from a real empty contacts list.
    bool contacts_data_hydration_pending = false;
    ContactsMode focused_filter_mode = ContactsMode::Contacts;
    bool focused_filter_mode_valid = false;

    // Data (using forward declaration, full type in .cpp)
    std::vector<chat::contacts::PeerDirectoryItem> reticulum_group_list;
    std::vector<chat::contacts::PeerDirectoryItem> display_list;
    std::vector<chat::contacts::PeerBrowseEntry> card_entries;
    chat::contacts::PeerDirectoryItem card_peer{};
    size_t card_index = 0;
    bool card_peer_valid = false;
    bool card_context_valid = false;
    ContactsMode card_mode = ContactsMode::Contacts;
    char card_query[32]{};
    lv_obj_t* node_card = nullptr;
    lv_obj_t* card_name = nullptr;
    lv_obj_t* card_identity = nullptr;
    lv_obj_t* card_region = nullptr;
    lv_obj_t* card_distance = nullptr;
    lv_obj_t* card_status = nullptr;
    lv_obj_t* card_counter = nullptr;
    platform::ui::map_search::RegionResult card_region_result{};
    uint32_t card_region_request = 0;
    int32_t card_region_latitude = 0, card_region_longitude = 0;
    bool card_region_coordinate_valid = false;
    char search_query[32] = {};

    // Timers
    lv_timer_t* refresh_timer = nullptr;

    // Modal windows
    ::ui::components::floating_search_box::State search_box;
    ::ui::components::floating_search_box::State lxmf_address_box;
    lv_obj_t* add_edit_modal = nullptr;
    lv_obj_t* add_edit_textarea = nullptr;
    lv_obj_t* add_edit_error_label = nullptr;
    lv_obj_t* reticulum_group_modal = nullptr;
    lv_obj_t* reticulum_group_name_textarea = nullptr;
    lv_obj_t* reticulum_group_destination_textarea = nullptr;
    lv_obj_t* reticulum_group_error_label = nullptr;
    lv_obj_t* del_confirm_modal = nullptr;
    lv_obj_t* action_menu_modal = nullptr;
    lv_obj_t* discover_modal = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_group_t* prev_group = nullptr;
    uint32_t modal_node_id = 0;
    bool modal_is_edit = false;
    bool reticulum_group_storage_supported = false;
    bool reticulum_group_storage_ready = false;
    bool reticulum_group_storage_loaded = false;
    char reticulum_group_storage_message[96] = {};
    char reticulum_group_storage_detail[128] = {};
    lv_timer_t* discover_scan_timer = nullptr;
    size_t discover_scan_start_nearby = 0;

    // Contacts has its own team conversation preview. Compose itself is always
    // routed to the single Chat app-owned implementation.
    chat::ui::ChatConversationScreen* conversation_screen = nullptr;
    lv_timer_t* conversation_timer = nullptr;

    // Node info screen
    lv_obj_t* node_info_root = nullptr;
    lv_group_t* node_info_group = nullptr;
    lv_group_t* node_info_prev_group = nullptr;
    bool reticulum_node_info_active = false;

    // Services (owned by AppContext)
    chat::contacts::ContactService* contact_service = nullptr;
    chat::ChatService* chat_service = nullptr;

    bool initialized = false;
    bool exiting = false;
};

extern ContactsPageState g_contacts_state;

inline bool uses_node_card()
{
    const auto mode = g_contacts_state.current_mode;
    return mode == ContactsMode::Contacts || mode == ContactsMode::Nearby || mode == ContactsMode::Ignored;
}
void refresh_node_card_data();
void hydrate_node_card();

} // namespace ui
} // namespace contacts
