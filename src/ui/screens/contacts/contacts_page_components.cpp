/**
 * @file contacts_page_components.cpp
 * @brief Contacts page UI components implementation (refactor: layout/styles split, behavior preserved)
 */

#include <Arduino.h> // Must be first for library compilation

#include "../../../app/app_context.h"
#include "../../../chat/usecase/chat_service.h"
#include "../../../chat/usecase/contact_service.h"
#include "../../../gps/gps_service_api.h"
#include "../../../team/protocol/team_chat.h"
#include "../../../team/protocol/team_position.h"
#include "../../ui_common.h"
#include "../../widgets/ime/ime_widget.h"
#include "../../widgets/system_notification.h"
#include "../chat/chat_compose_components.h"
#include "../chat/chat_conversation_components.h"
#include "../node_info/node_info_page_components.h"
#include "../team/team_state.h"
#include "../team/team_ui_store.h"
#include "contacts_page_components.h"
#include "contacts_page_input.h"
#include "contacts_page_layout.h"
#include "contacts_page_styles.h"
#include "contacts_state.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#define CONTACTS_DEBUG 0
#if CONTACTS_DEBUG
#define CONTACTS_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CONTACTS_LOG(...)
#endif

using namespace contacts::ui;

lv_obj_t* ui_chat_get_container();

static constexpr int kItemsPerPage = 4;
static constexpr int kButtonHeight = 28;
static constexpr int kBottomBtnMinWidth = 50;
static constexpr int kBottomBtnPadH = 8;

// UI color tokens (must align with docs/skyplot.md)
static constexpr uint32_t kColorAmber = 0xEBA341;
static constexpr uint32_t kColorAmberDark = 0xC98118;
static constexpr uint32_t kColorPanelBg = 0xFAF0D8;
static constexpr uint32_t kColorLine = 0xE7C98F;
static constexpr uint32_t kColorText = 0x6B4A1E;
static constexpr uint32_t kColorWarn = 0xB94A2C;

static lv_group_t* s_compose_group = nullptr;
static lv_group_t* s_compose_prev_group = nullptr;
static uint32_t s_compose_peer_id = 0;
static chat::ChannelId s_compose_channel = chat::ChannelId::PRIMARY;
static chat::MeshProtocol s_compose_protocol = chat::MeshProtocol::Meshtastic;
static bool s_refreshing_ui = false;
static lv_group_t* s_conv_group = nullptr;
static lv_group_t* s_conv_prev_group = nullptr;
static bool s_compose_from_conversation = false;
static bool s_compose_is_team = false;
static std::string s_last_sent_text;
static uint32_t s_last_sent_ts = 0;
static uint32_t s_team_msg_id = 1;

// --- Forward declarations (same behavior as original) ---
static void contacts_btn_event_handler(lv_event_t* e);
static void nearby_btn_event_handler(lv_event_t* e);
static std::string format_time_status(uint32_t last_seen);
static std::string format_snr(float snr);

static void on_filter_focused(lv_event_t* e);
static void on_filter_clicked(lv_event_t* e);
static void on_list_item_clicked(lv_event_t* e);
static void on_prev_clicked(lv_event_t* e);
static void on_next_clicked(lv_event_t* e);
static void on_back_clicked(lv_event_t* e);
static void open_action_menu_modal();
static void on_action_menu_item_clicked(lv_event_t* e);
static void on_action_menu_key(lv_event_t* e);
static lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text);
static const chat::contacts::NodeInfo* get_selected_node();
static bool get_selected_broadcast_target(chat::MeshProtocol* out_protocol,
                                          chat::ChannelId* out_channel,
                                          const char** out_title);
static void open_add_edit_modal(bool is_edit);
static void open_delete_confirm_modal();
static void open_node_info_screen();
static void close_node_info_screen();
static void modal_close(lv_obj_t*& modal_obj);
static void modal_prepare_group();
static void modal_restore_group();
static lv_obj_t* create_modal_root(int width, int height);
static bool is_any_modal_open();
static void on_add_edit_save_clicked(lv_event_t* e);
static void on_add_edit_cancel_clicked(lv_event_t* e);
static void on_del_confirm_clicked(lv_event_t* e);
static void on_del_cancel_clicked(lv_event_t* e);
static void on_discovery_scan_done(lv_timer_t* timer);
static void execute_discovery_command(uint8_t command_index);
static void on_node_info_back_clicked(lv_event_t* e);
static void open_chat_compose();
static void close_chat_compose();
static void on_compose_action(chat::ui::ChatComposeScreen::ActionIntent intent, void* user_data);
static void on_compose_back(void* user_data);
static void on_compose_send_done(bool ok, bool timeout, void* user_data);
static void open_team_conversation();
static void close_team_conversation();
static void refresh_team_conversation();
static void on_team_conversation_action(chat::ui::ChatConversationScreen::ActionIntent intent, void* user_data);
static void on_team_conversation_back(void* user_data);
static void send_team_position();
static void refresh_filter_checked_state();

// Forward declaration - actual implementation moved to ui_contacts.cpp
// to avoid library compilation issues with Arduino framework dependencies
extern void refresh_contacts_data_impl();

static void apply_primary_text(lv_obj_t* label)
{
    if (!label) return;
    contacts::ui::style::apply_label_primary(label);
}

static void refresh_filter_checked_state()
{
    if (g_contacts_state.contacts_btn == nullptr ||
        g_contacts_state.nearby_btn == nullptr ||
        g_contacts_state.broadcast_btn == nullptr)
    {
        return;
    }

    lv_obj_clear_state(g_contacts_state.contacts_btn, LV_STATE_CHECKED);
    lv_obj_clear_state(g_contacts_state.nearby_btn, LV_STATE_CHECKED);
    lv_obj_clear_state(g_contacts_state.broadcast_btn, LV_STATE_CHECKED);
    if (g_contacts_state.team_btn)
    {
        lv_obj_clear_state(g_contacts_state.team_btn, LV_STATE_CHECKED);
    }
    if (g_contacts_state.discover_btn)
    {
        lv_obj_clear_state(g_contacts_state.discover_btn, LV_STATE_CHECKED);
    }

    if (g_contacts_state.current_mode == ContactsMode::Contacts)
    {
        lv_obj_add_state(g_contacts_state.contacts_btn, LV_STATE_CHECKED);
    }
    else if (g_contacts_state.current_mode == ContactsMode::Nearby)
    {
        lv_obj_add_state(g_contacts_state.nearby_btn, LV_STATE_CHECKED);
    }
    else if (g_contacts_state.current_mode == ContactsMode::Broadcast)
    {
        lv_obj_add_state(g_contacts_state.broadcast_btn, LV_STATE_CHECKED);
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team && g_contacts_state.team_btn)
    {
        lv_obj_add_state(g_contacts_state.team_btn, LV_STATE_CHECKED);
    }
    else if (g_contacts_state.current_mode == ContactsMode::Discover && g_contacts_state.discover_btn)
    {
        lv_obj_add_state(g_contacts_state.discover_btn, LV_STATE_CHECKED);
    }
}

static lv_obj_t* create_bottom_bar_button(lv_obj_t* parent,
                                          const char* text,
                                          uint32_t bg_color,
                                          lv_event_cb_t cb)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_height(btn, kButtonHeight);
    lv_obj_set_style_pad_hor(btn, kBottomBtnPadH, LV_PART_MAIN);
    contacts::ui::style::apply_btn_basic(btn);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_color), LV_PART_MAIN);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    apply_primary_text(label);
    lv_obj_update_layout(label);
    lv_coord_t width = lv_obj_get_width(label) + (kBottomBtnPadH * 2);
    if (width < kBottomBtnMinWidth)
    {
        width = kBottomBtnMinWidth;
    }
    lv_obj_set_width(btn, width);
    lv_obj_center(label);

    if (cb)
    {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    }
    return btn;
}

void refresh_contacts_data()
{
    refresh_contacts_data_impl();
    CONTACTS_LOG("[Contacts] contacts=%u nearby=%u\n",
                 (unsigned)g_contacts_state.contacts_list.size(),
                 (unsigned)g_contacts_state.nearby_list.size());
}

// ---------------- Formatting helpers ----------------

static std::string format_time_status(uint32_t last_seen)
{
    uint32_t now_secs = time(nullptr);
    if (now_secs < last_seen)
    {
        return "Offline";
    }

    uint32_t age_secs = now_secs - last_seen;

    // Online: ≤ 2 minutes
    if (age_secs <= 120)
    {
        return "Online";
    }

    // Minutes: 3-59 minutes
    if (age_secs < 3600)
    {
        uint32_t minutes = age_secs / 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "Seen %um", minutes);
        return std::string(buf);
    }

    // Hours: 1-23 hours
    if (age_secs < 86400)
    {
        uint32_t hours = age_secs / 3600;
        char buf[16];
        snprintf(buf, sizeof(buf), "Seen %uh", hours);
        return std::string(buf);
    }

    // Days: 1-6 days
    if (age_secs < 6 * 86400)
    {
        uint32_t days = age_secs / 86400;
        char buf[16];
        snprintf(buf, sizeof(buf), "Seen %ud", days);
        return std::string(buf);
    }

    // > 6 days: should be filtered out
    return "Offline";
}

static std::string format_snr(float snr)
{
    if (snr == 0.0f)
    {
        return "SNR -";
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "SNR %.0f", snr);
    return std::string(buf);
}

static chat::MeshProtocol active_mesh_protocol()
{
    return app::AppContext::getInstance().getConfig().mesh_protocol;
}

static const char* mesh_protocol_short_label(chat::MeshProtocol protocol)
{
    return (protocol == chat::MeshProtocol::MeshCore) ? "MC" : "MT";
}

static const char* node_protocol_short_label(chat::contacts::NodeProtocolType protocol)
{
    switch (protocol)
    {
    case chat::contacts::NodeProtocolType::MeshCore:
        return "MC";
    case chat::contacts::NodeProtocolType::Meshtastic:
        return "MT";
    default:
        return "";
    }
}

static bool node_protocol_to_mesh(chat::contacts::NodeProtocolType protocol, chat::MeshProtocol* out)
{
    if (!out)
    {
        return false;
    }
    switch (protocol)
    {
    case chat::contacts::NodeProtocolType::MeshCore:
        *out = chat::MeshProtocol::MeshCore;
        return true;
    case chat::contacts::NodeProtocolType::Meshtastic:
        *out = chat::MeshProtocol::Meshtastic;
        return true;
    default:
        return false;
    }
}

struct BroadcastTargetSpec
{
    chat::MeshProtocol protocol;
    chat::ChannelId channel;
    const char* label;
};

enum class DiscoveryActionCommand : uint8_t
{
    ScanLocal = 0,
    SendIdLocal = 1,
    SendIdBroadcast = 2,
    Cancel = 3,
};

struct DiscoveryActionSpec
{
    const char* label;
    const char* status;
    DiscoveryActionCommand command;
};

static constexpr DiscoveryActionSpec kDiscoveryActionSpecs[] = {
    {"Scan Local", "5s", DiscoveryActionCommand::ScanLocal},
    {"Send ID Local", "Local", DiscoveryActionCommand::SendIdLocal},
    {"Send ID Broadcast", "Bcast", DiscoveryActionCommand::SendIdBroadcast},
    {"Cancel", "Back", DiscoveryActionCommand::Cancel},
};

static bool get_broadcast_target_spec(int index, BroadcastTargetSpec* out)
{
    if (!out)
    {
        return false;
    }
    switch (index)
    {
    case 0:
        *out = {chat::MeshProtocol::Meshtastic, chat::ChannelId::PRIMARY, "[MT] Primary"};
        return true;
    case 1:
        *out = {chat::MeshProtocol::Meshtastic, chat::ChannelId::SECONDARY, "[MT] Secondary"};
        return true;
    case 2:
        *out = {chat::MeshProtocol::MeshCore, chat::ChannelId::PRIMARY, "[MC] Primary"};
        return true;
    case 3:
        *out = {chat::MeshProtocol::MeshCore, chat::ChannelId::SECONDARY, "[MC] Secondary"};
        return true;
    default:
        return false;
    }
}

static bool get_discovery_action_spec(int index, DiscoveryActionSpec* out)
{
    if (!out)
    {
        return false;
    }
    if (index < 0 || index >= static_cast<int>(sizeof(kDiscoveryActionSpecs) / sizeof(kDiscoveryActionSpecs[0])))
    {
        return false;
    }
    *out = kDiscoveryActionSpecs[index];
    return true;
}

static const char* team_command_name(team::proto::TeamCommandType type)
{
    switch (type)
    {
    case team::proto::TeamCommandType::RallyTo:
        return "RallyTo";
    case team::proto::TeamCommandType::MoveTo:
        return "MoveTo";
    case team::proto::TeamCommandType::Hold:
        return "Hold";
    default:
        return "Command";
    }
}

static std::string truncate_text(const std::string& text, size_t max_len)
{
    if (text.size() <= max_len)
    {
        return text;
    }
    if (max_len <= 3)
    {
        return text.substr(0, max_len);
    }
    return text.substr(0, max_len - 3) + "...";
}

static std::string format_team_chat_entry(const team::ui::TeamChatLogEntry& entry)
{
    if (entry.type == team::proto::TeamChatType::Text)
    {
        std::string text(entry.payload.begin(), entry.payload.end());
        return truncate_text(text, 160);
    }
    if (entry.type == team::proto::TeamChatType::Location)
    {
        team::proto::TeamChatLocation loc;
        if (team::proto::decodeTeamChatLocation(entry.payload.data(), entry.payload.size(), &loc))
        {
            double lat = static_cast<double>(loc.lat_e7) / 1e7;
            double lon = static_cast<double>(loc.lon_e7) / 1e7;
            char buf[128];
            char coord_buf[64];
            uint8_t coord_fmt = app::AppContext::getInstance().getConfig().gps_coord_format;
            ui_format_coords(lat, lon, coord_fmt, coord_buf, sizeof(coord_buf));
            if (!loc.label.empty())
            {
                snprintf(buf, sizeof(buf), "Location: %s %s", loc.label.c_str(), coord_buf);
            }
            else
            {
                snprintf(buf, sizeof(buf), "Location: %s", coord_buf);
            }
            return std::string(buf);
        }
        return "Location";
    }
    if (entry.type == team::proto::TeamChatType::Command)
    {
        team::proto::TeamChatCommand cmd;
        if (team::proto::decodeTeamChatCommand(entry.payload.data(), entry.payload.size(), &cmd))
        {
            const char* name = team_command_name(cmd.cmd_type);
            double lat = static_cast<double>(cmd.lat_e7) / 1e7;
            double lon = static_cast<double>(cmd.lon_e7) / 1e7;
            char buf[160];
            char coord_buf[64];
            uint8_t coord_fmt = app::AppContext::getInstance().getConfig().gps_coord_format;
            ui_format_coords(lat, lon, coord_fmt, coord_buf, sizeof(coord_buf));
            if (cmd.lat_e7 != 0 || cmd.lon_e7 != 0)
            {
                if (!cmd.note.empty())
                {
                    snprintf(buf, sizeof(buf), "Command: %s %s %s",
                             name, coord_buf, cmd.note.c_str());
                }
                else
                {
                    snprintf(buf, sizeof(buf), "Command: %s %s",
                             name, coord_buf);
                }
            }
            else if (!cmd.note.empty())
            {
                snprintf(buf, sizeof(buf), "Command: %s %s", name, cmd.note.c_str());
            }
            else
            {
                snprintf(buf, sizeof(buf), "Command: %s", name);
            }
            return std::string(buf);
        }
        return "Command";
    }
    return "Message";
}

static void refresh_team_state_from_store()
{
    if (team::ui::g_team_state.in_team && team::ui::g_team_state.has_team_id)
    {
        return;
    }
    team::ui::TeamUiSnapshot snap;
    if (!team::ui::team_ui_get_store().load(snap))
    {
        return;
    }
    team::ui::g_team_state.in_team = snap.in_team;
    team::ui::g_team_state.has_team_id = snap.has_team_id;
    team::ui::g_team_state.team_id = snap.team_id;
    team::ui::g_team_state.team_name = snap.team_name;
    team::ui::g_team_state.has_team_psk = snap.has_team_psk;
    team::ui::g_team_state.security_round = snap.security_round;
    if (snap.has_team_psk)
    {
        team::ui::g_team_state.team_psk = snap.team_psk;
    }
    team::ui::g_team_state.members = snap.members;
}

static bool is_team_available()
{
    refresh_team_state_from_store();
    // Team chat should be reachable once we know a team_id (e.g. after receiving TEAM_CHAT).
    return team::ui::g_team_state.has_team_id;
}

// ---------------- Panel creation (public API) ----------------

void create_filter_panel(lv_obj_t* parent)
{
    // Structure + styles handled in layout/styles
    contacts::ui::layout::create_filter_panel(parent);

    // Bind events:
    // - Rotate in Filter column triggers FOCUSED -> switch mode + refresh
    // - Press in Filter column:
    //    * on TopBar back -> exit (handled by topbar)
    //    * on Contacts/Nearby -> move focus to List column
    if (g_contacts_state.contacts_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.contacts_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(g_contacts_state.contacts_btn, on_filter_clicked, LV_EVENT_CLICKED, nullptr);
    }
    if (g_contacts_state.nearby_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.nearby_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(g_contacts_state.nearby_btn, on_filter_clicked, LV_EVENT_CLICKED, nullptr);
    }
    if (g_contacts_state.broadcast_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.broadcast_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(g_contacts_state.broadcast_btn, on_filter_clicked, LV_EVENT_CLICKED, nullptr);
    }
    if (g_contacts_state.team_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.team_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(g_contacts_state.team_btn, on_filter_clicked, LV_EVENT_CLICKED, nullptr);
    }
    if (g_contacts_state.discover_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.discover_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        lv_obj_add_event_cb(g_contacts_state.discover_btn, on_filter_clicked, LV_EVENT_CLICKED, nullptr);
    }

    // Keep highlight consistent with mode using CHECKED state
    // (visual-only; does not change behavior)
    refresh_filter_checked_state();
}

void create_list_panel(lv_obj_t* parent)
{
    contacts::ui::layout::create_list_panel(parent);
}

void create_action_panel(lv_obj_t* parent)
{
    contacts::ui::layout::create_action_panel(parent);
    // Buttons will be created/updated in refresh_ui() based on selected item (unchanged)
}

// ---------------- Filter handlers (unchanged behavior) ----------------

static void contacts_btn_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        CONTACTS_LOG("[Contacts] Contacts button clicked\n");
        g_contacts_state.current_mode = ContactsMode::Contacts;
        g_contacts_state.current_page = 0; // Reset to first page
        refresh_ui();
    }
}

static void nearby_btn_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        CONTACTS_LOG("[Contacts] Nearby button clicked\n");
        g_contacts_state.current_mode = ContactsMode::Nearby;
        g_contacts_state.current_page = 0; // Reset to first page
        refresh_ui();
    }
}

static void on_filter_focused(lv_event_t* e)
{
    lv_obj_t* tgt = (lv_obj_t*)lv_event_get_target(e);
    ContactsMode new_mode = g_contacts_state.current_mode;
    if (tgt == g_contacts_state.contacts_btn) new_mode = ContactsMode::Contacts;
    else if (tgt == g_contacts_state.nearby_btn) new_mode = ContactsMode::Nearby;
    else if (tgt == g_contacts_state.broadcast_btn) new_mode = ContactsMode::Broadcast;
    else if (tgt == g_contacts_state.team_btn) new_mode = ContactsMode::Team;
    else if (tgt == g_contacts_state.discover_btn) new_mode = ContactsMode::Discover;
    else return;

    if (new_mode != g_contacts_state.current_mode)
    {
        if (new_mode == ContactsMode::Discover &&
            g_contacts_state.current_mode != ContactsMode::Discover)
        {
            g_contacts_state.last_action_mode = g_contacts_state.current_mode;
        }
        g_contacts_state.current_mode = new_mode;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
        refresh_contacts_data();
        refresh_ui(); // 旋转到另一个按钮，就刷新第二列
        return;
    }

    refresh_filter_checked_state();
}

static void on_filter_clicked(lv_event_t* e)
{
    lv_obj_t* tgt = (lv_obj_t*)lv_event_get_target(e);
    if (tgt == g_contacts_state.discover_btn &&
        g_contacts_state.current_mode != ContactsMode::Discover)
    {
        g_contacts_state.last_action_mode = g_contacts_state.current_mode;
        g_contacts_state.current_mode = ContactsMode::Discover;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
        refresh_contacts_data();
        refresh_ui();
    }

    // Press on filter mode button: move focus to List column
    contacts_focus_to_list();
}

static void on_list_item_clicked(lv_event_t* e)
{
    lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
    g_contacts_state.selected_index = (int)(intptr_t)lv_obj_get_user_data(item);
    if (g_contacts_state.current_mode == ContactsMode::Discover)
    {
        execute_discovery_command(static_cast<uint8_t>(g_contacts_state.selected_index));
        return;
    }
    open_action_menu_modal();
}

static void on_prev_clicked(lv_event_t* /*e*/)
{
    if (lv_obj_has_state(g_contacts_state.prev_btn, LV_STATE_DISABLED)) return;
    g_contacts_state.current_page--;
    if (g_contacts_state.current_page < 0)
    {
        // 循环：跳到最后一页
        int total_pages = (g_contacts_state.total_items + kItemsPerPage - 1) / kItemsPerPage;
        if (total_pages <= 0) total_pages = 1;
        g_contacts_state.current_page = total_pages - 1;
    }
    g_contacts_state.selected_index = -1;
    refresh_ui();
    contacts_focus_to_list();
}

static void on_next_clicked(lv_event_t* /*e*/)
{
    if (lv_obj_has_state(g_contacts_state.next_btn, LV_STATE_DISABLED)) return;
    int total_pages = (g_contacts_state.total_items + kItemsPerPage - 1) / kItemsPerPage;
    if (total_pages <= 0) total_pages = 1;

    g_contacts_state.current_page++;
    if (g_contacts_state.current_page >= total_pages)
    {
        // 循环：回到第一页
        g_contacts_state.current_page = 0;
    }
    g_contacts_state.selected_index = -1;
    refresh_ui();
    contacts_focus_to_list();
}

static void on_back_clicked(lv_event_t* /*e*/)
{
    contacts_focus_to_filter();
}

static const chat::contacts::NodeInfo* get_selected_node()
{
    if (g_contacts_state.current_mode == ContactsMode::Broadcast ||
        g_contacts_state.current_mode == ContactsMode::Team ||
        g_contacts_state.current_mode == ContactsMode::Discover)
    {
        return nullptr;
    }
    if (g_contacts_state.selected_index < 0)
    {
        return nullptr;
    }
    const auto& list = (g_contacts_state.current_mode == ContactsMode::Contacts)
                           ? g_contacts_state.contacts_list
                           : g_contacts_state.nearby_list;
    if (g_contacts_state.selected_index >= static_cast<int>(list.size()))
    {
        return nullptr;
    }
    return &list[g_contacts_state.selected_index];
}

static bool get_selected_broadcast_target(chat::MeshProtocol* out_protocol,
                                          chat::ChannelId* out_channel,
                                          const char** out_title)
{
    if (g_contacts_state.current_mode != ContactsMode::Broadcast ||
        !out_protocol || !out_channel || !out_title)
    {
        return false;
    }
    BroadcastTargetSpec spec{};
    if (!get_broadcast_target_spec(g_contacts_state.selected_index, &spec))
    {
        return false;
    }
    *out_protocol = spec.protocol;
    *out_channel = spec.channel;
    *out_title = spec.label;
    return true;
}

static void modal_prepare_group()
{
    if (!g_contacts_state.modal_group)
    {
        g_contacts_state.modal_group = lv_group_create();
    }
    lv_group_remove_all_objs(g_contacts_state.modal_group);
    g_contacts_state.prev_group = lv_group_get_default();
    lv_group_t* contacts_group = contacts_input_get_group();
    if (contacts_group && g_contacts_state.prev_group != contacts_group)
    {
        g_contacts_state.prev_group = contacts_group;
    }
    set_default_group(g_contacts_state.modal_group);
}

static void modal_restore_group()
{
    lv_group_t* restore = g_contacts_state.prev_group;
    if (!restore)
    {
        restore = contacts_input_get_group();
    }
    if (restore)
    {
        set_default_group(restore);
    }
    g_contacts_state.prev_group = nullptr;
    contacts_input_on_ui_refreshed();
}

static lv_obj_t* create_modal_root(int width, int height)
{
    lv_obj_t* screen = lv_screen_active();
    lv_coord_t screen_w = lv_obj_get_width(screen);
    lv_coord_t screen_h = lv_obj_get_height(screen);

    lv_obj_t* bg = lv_obj_create(screen);
    lv_obj_set_size(bg, screen_w, screen_h);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(kColorText), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, width, height);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_hex(kColorPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(kColorLine), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 8, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    return bg;
}

static void modal_close(lv_obj_t*& modal_obj)
{
    if (modal_obj)
    {
        lv_obj_del(modal_obj);
        modal_obj = nullptr;
    }
    modal_restore_group();
}

static bool is_any_modal_open()
{
    return g_contacts_state.add_edit_modal != nullptr ||
           g_contacts_state.del_confirm_modal != nullptr ||
           g_contacts_state.action_menu_modal != nullptr ||
           g_contacts_state.discover_modal != nullptr;
}

static void open_add_edit_modal(bool is_edit)
{
    if (g_contacts_state.add_edit_modal)
    {
        return;
    }
    const auto* node = get_selected_node();
    if (!node)
    {
        return;
    }

    g_contacts_state.modal_is_edit = is_edit;
    g_contacts_state.modal_node_id = node->node_id;

    modal_prepare_group();
    g_contacts_state.add_edit_modal = create_modal_root(280, 160);
    lv_obj_t* win = lv_obj_get_child(g_contacts_state.add_edit_modal, 0);

    lv_obj_t* title = lv_label_create(win);
    lv_label_set_text(title, is_edit ? "Edit nickname" : "Enter nickname");
    apply_primary_text(title);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    g_contacts_state.add_edit_textarea = lv_textarea_create(win);
    lv_textarea_set_one_line(g_contacts_state.add_edit_textarea, true);
    lv_textarea_set_max_length(g_contacts_state.add_edit_textarea, 12);
    lv_obj_set_width(g_contacts_state.add_edit_textarea, LV_PCT(100));
    lv_obj_align(g_contacts_state.add_edit_textarea, LV_ALIGN_TOP_MID, 0, 26);

    if (is_edit)
    {
        lv_textarea_set_text(g_contacts_state.add_edit_textarea, node->display_name.c_str());
        lv_textarea_set_cursor_pos(g_contacts_state.add_edit_textarea, LV_TEXTAREA_CURSOR_LAST);
    }

    g_contacts_state.add_edit_error_label = lv_label_create(win);
    lv_label_set_text(g_contacts_state.add_edit_error_label, "");
    lv_obj_set_style_text_color(g_contacts_state.add_edit_error_label, lv_color_hex(kColorWarn), 0);
    lv_obj_align(g_contacts_state.add_edit_error_label, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_add_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* save_btn = lv_btn_create(btn_row);
    lv_obj_set_size(save_btn, 90, 28);
    contacts::ui::style::apply_btn_basic(save_btn);
    lv_obj_t* save_label = lv_label_create(save_btn);
    lv_label_set_text(save_label, "Save");
    apply_primary_text(save_label);
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, on_add_edit_save_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 90, 28);
    contacts::ui::style::apply_btn_basic(cancel_btn);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    apply_primary_text(cancel_label);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_add_edit_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g_contacts_state.modal_group, g_contacts_state.add_edit_textarea);
    lv_group_add_obj(g_contacts_state.modal_group, save_btn);
    lv_group_add_obj(g_contacts_state.modal_group, cancel_btn);
    lv_group_focus_obj(g_contacts_state.add_edit_textarea);
}

static void open_delete_confirm_modal()
{
    if (g_contacts_state.del_confirm_modal)
    {
        return;
    }
    const auto* node = get_selected_node();
    if (!node)
    {
        return;
    }

    g_contacts_state.modal_node_id = node->node_id;
    modal_prepare_group();
    g_contacts_state.del_confirm_modal = create_modal_root(280, 140);
    lv_obj_t* win = lv_obj_get_child(g_contacts_state.del_confirm_modal, 0);

    char msg[64];
    snprintf(msg, sizeof(msg), "Delete contact %s?", node->display_name.c_str());
    lv_obj_t* label = lv_label_create(win);
    lv_label_set_text(label, msg);
    apply_primary_text(label);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* btn_row = lv_obj_create(win);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* confirm_btn = lv_btn_create(btn_row);
    lv_obj_set_size(confirm_btn, 90, 28);
    contacts::ui::style::apply_btn_basic(confirm_btn);
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "Confirm");
    apply_primary_text(confirm_label);
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm_btn, on_del_confirm_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 90, 28);
    contacts::ui::style::apply_btn_basic(cancel_btn);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    apply_primary_text(cancel_label);
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_del_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g_contacts_state.modal_group, confirm_btn);
    lv_group_add_obj(g_contacts_state.modal_group, cancel_btn);
    lv_group_focus_obj(cancel_btn);
}

static void open_node_info_screen()
{
    if (g_contacts_state.node_info_root)
    {
        return;
    }
    const auto* node = get_selected_node();
    if (!node)
    {
        return;
    }

    lv_obj_t* parent = g_contacts_state.root
                           ? lv_obj_get_parent(g_contacts_state.root)
                           : lv_screen_active();
    if (!parent)
    {
        return;
    }

    node_info::ui::NodeInfoWidgets widgets = node_info::ui::create(parent);
    g_contacts_state.node_info_root = widgets.root;

    const chat::contacts::NodeInfo* info = node;
    if (g_contacts_state.contact_service)
    {
        const auto* latest = g_contacts_state.contact_service->getNodeInfo(node->node_id);
        if (latest)
        {
            info = latest;
        }
    }
    node_info::ui::set_node_info(*info);

    if (!g_contacts_state.node_info_group)
    {
        g_contacts_state.node_info_group = lv_group_create();
    }
    lv_group_remove_all_objs(g_contacts_state.node_info_group);
    g_contacts_state.node_info_prev_group = lv_group_get_default();
    set_default_group(g_contacts_state.node_info_group);

    if (widgets.back_btn)
    {
        lv_group_add_obj(g_contacts_state.node_info_group, widgets.back_btn);
        lv_group_focus_obj(widgets.back_btn);
        lv_obj_add_event_cb(widgets.back_btn, on_node_info_back_clicked, LV_EVENT_CLICKED, nullptr);
    }

    if (g_contacts_state.root)
    {
        lv_obj_add_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_contacts_state.refresh_timer)
    {
        lv_timer_pause(g_contacts_state.refresh_timer);
    }
}

static void close_node_info_screen()
{
    if (!g_contacts_state.node_info_root)
    {
        return;
    }

    node_info::ui::destroy();
    g_contacts_state.node_info_root = nullptr;

    if (g_contacts_state.node_info_group)
    {
        lv_group_remove_all_objs(g_contacts_state.node_info_group);
    }

    lv_group_t* restore = contacts_input_get_group();
    if (restore)
    {
        set_default_group(restore);
    }
    g_contacts_state.node_info_prev_group = nullptr;

    if (g_contacts_state.root)
    {
        lv_obj_clear_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_contacts_state.refresh_timer)
    {
        lv_timer_resume(g_contacts_state.refresh_timer);
    }

    refresh_ui();
    contacts_focus_to_list();
}

static void open_chat_compose()
{
    if (g_contacts_state.compose_screen)
    {
        return;
    }
    if (!g_contacts_state.conversation_screen)
    {
        s_compose_from_conversation = false;
    }
    const auto* node = get_selected_node();
    if (g_contacts_state.current_mode != ContactsMode::Broadcast &&
        g_contacts_state.current_mode != ContactsMode::Team &&
        !node)
    {
        return;
    }
    if (g_contacts_state.current_mode == ContactsMode::Team && !is_team_available())
    {
        return;
    }

    lv_obj_t* parent = g_contacts_state.root
                           ? lv_obj_get_parent(g_contacts_state.root)
                           : lv_screen_active();
    if (lv_obj_t* chat_parent = ui_chat_get_container())
    {
        if (lv_obj_is_valid(chat_parent))
        {
            parent = chat_parent;
        }
    }

    chat::ChannelId channel = chat::ChannelId::PRIMARY;
    uint32_t peer_id = 0;
    chat::MeshProtocol protocol = active_mesh_protocol();
    std::string title;
    if (g_contacts_state.current_mode == ContactsMode::Broadcast)
    {
        chat::MeshProtocol target_protocol = protocol;
        chat::ChannelId target_channel = chat::ChannelId::PRIMARY;
        const char* target_title = nullptr;
        if (!get_selected_broadcast_target(&target_protocol, &target_channel, &target_title))
        {
            return;
        }
        if (target_protocol != active_mesh_protocol())
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "Switch to %s to chat",
                     (target_protocol == chat::MeshProtocol::MeshCore) ? "MeshCore" : "Meshtastic");
            ::ui::SystemNotification::show(buf, 2200);
            return;
        }
        protocol = target_protocol;
        channel = target_channel;
        peer_id = 0;
        title = target_title ? target_title : "Broadcast";
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        channel = chat::ChannelId::PRIMARY;
        peer_id = 0;
        title = team::ui::g_team_state.team_name.empty()
                    ? "Team"
                    : team::ui::g_team_state.team_name;
    }
    else
    {
        channel = chat::ChannelId::PRIMARY;
        peer_id = node->node_id;
        chat::MeshProtocol node_protocol = protocol;
        if (node_protocol_to_mesh(node->protocol, &node_protocol) &&
            node_protocol != protocol)
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "Switch to %s to chat",
                     (node_protocol == chat::MeshProtocol::MeshCore) ? "MeshCore" : "Meshtastic");
            ::ui::SystemNotification::show(buf, 2200);
            return;
        }
        if (g_contacts_state.contact_service)
        {
            title = g_contacts_state.contact_service->getContactName(node->node_id);
        }
        if (title.empty())
        {
            title = node->display_name;
        }
    }

    s_compose_prev_group = lv_group_get_default();
    if (!s_compose_group)
    {
        s_compose_group = lv_group_create();
    }
    lv_group_remove_all_objs(s_compose_group);
    set_default_group(s_compose_group);

    chat::ConversationId conv(channel, peer_id, protocol);
    g_contacts_state.compose_screen = new chat::ui::ChatComposeScreen(parent, conv);
    g_contacts_state.compose_screen->setActionCallback(on_compose_action, nullptr);
    g_contacts_state.compose_screen->setBackCallback(on_compose_back, nullptr);

    if (!g_contacts_state.compose_ime)
    {
        g_contacts_state.compose_ime = new ::ui::widgets::ImeWidget();
    }
    lv_obj_t* compose_content = g_contacts_state.compose_screen->getContent();
    lv_obj_t* compose_textarea = g_contacts_state.compose_screen->getTextarea();
    if (compose_content && compose_textarea)
    {
        g_contacts_state.compose_ime->init(compose_content, compose_textarea);
        g_contacts_state.compose_screen->attachImeWidget(g_contacts_state.compose_ime);
        if (lv_group_t* g = lv_group_get_default())
        {
            lv_group_add_obj(g, g_contacts_state.compose_ime->focus_obj());
        }
    }

    std::string header = "[" + std::string(mesh_protocol_short_label(protocol)) + "] " + title;
    g_contacts_state.compose_screen->setHeaderText(header.c_str(), nullptr);
    s_compose_peer_id = peer_id;
    s_compose_channel = channel;
    s_compose_protocol = protocol;
    s_compose_is_team = (g_contacts_state.current_mode == ContactsMode::Team);
    if (s_compose_is_team)
    {
        g_contacts_state.compose_screen->setActionLabels("Send", "Cancel");
        g_contacts_state.compose_screen->setPositionButton("Position", true);
    }
    else
    {
        g_contacts_state.compose_screen->setPositionButton(nullptr, false);
    }

    if (s_compose_from_conversation && g_contacts_state.conversation_screen)
    {
        lv_obj_add_flag(g_contacts_state.conversation_screen->getObj(), LV_OBJ_FLAG_HIDDEN);
        if (g_contacts_state.conversation_timer)
        {
            lv_timer_pause(g_contacts_state.conversation_timer);
        }
    }
    else
    {
        if (g_contacts_state.root)
        {
            lv_obj_add_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
        }
        if (g_contacts_state.refresh_timer)
        {
            lv_timer_pause(g_contacts_state.refresh_timer);
        }
    }
}

static void close_chat_compose()
{
    if (!g_contacts_state.compose_screen)
    {
        return;
    }
    if (g_contacts_state.compose_ime)
    {
        g_contacts_state.compose_ime->detach();
        delete g_contacts_state.compose_ime;
        g_contacts_state.compose_ime = nullptr;
    }
    delete g_contacts_state.compose_screen;
    g_contacts_state.compose_screen = nullptr;
    s_compose_peer_id = 0;
    s_compose_channel = chat::ChannelId::PRIMARY;
    s_compose_protocol = chat::MeshProtocol::Meshtastic;
    s_compose_is_team = false;

    if (s_compose_from_conversation && g_contacts_state.conversation_screen)
    {
        lv_obj_clear_flag(g_contacts_state.conversation_screen->getObj(), LV_OBJ_FLAG_HIDDEN);
        if (g_contacts_state.conversation_timer)
        {
            lv_timer_resume(g_contacts_state.conversation_timer);
        }
        s_compose_from_conversation = false;
        s_compose_prev_group = nullptr;
        if (s_conv_group)
        {
            set_default_group(s_conv_group);
        }
    }
    else
    {
        if (g_contacts_state.root)
        {
            lv_obj_clear_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
        }
        if (g_contacts_state.refresh_timer)
        {
            lv_timer_resume(g_contacts_state.refresh_timer);
        }
        lv_group_t* contacts_group = contacts_input_get_group();
        if (contacts_group)
        {
            set_default_group(contacts_group);
        }
        else if (s_compose_prev_group)
        {
            set_default_group(s_compose_prev_group);
        }
        s_compose_prev_group = nullptr;
        contacts_focus_to_list();
        refresh_ui();
    }
}

static void on_compose_action(chat::ui::ChatComposeScreen::ActionIntent intent, void* /*user_data*/)
{
    if ((intent == chat::ui::ChatComposeScreen::ActionIntent::Send ||
         intent == chat::ui::ChatComposeScreen::ActionIntent::Position) &&
        g_contacts_state.compose_screen)
    {
        if (s_compose_is_team)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            team::TeamController* controller = app_ctx.getTeamController();
            if (!controller || !is_team_available())
            {
                ::ui::SystemNotification::show("Team chat send failed", 2000);
                close_chat_compose();
                return;
            }
            if (!team::ui::g_team_state.has_team_psk)
            {
                ::ui::SystemNotification::show("Team keys not ready", 2000);
                close_chat_compose();
                return;
            }
            if (!controller->setKeysFromPsk(team::ui::g_team_state.team_id,
                                            team::ui::g_team_state.security_round,
                                            team::ui::g_team_state.team_psk.data(),
                                            team::ui::g_team_state.team_psk.size()))
            {
                ::ui::SystemNotification::show("Team keys not ready", 2000);
                close_chat_compose();
                return;
            }
            uint32_t ts = static_cast<uint32_t>(time(nullptr));
            if (ts < 1577836800U)
            {
                ts = static_cast<uint32_t>(millis() / 1000);
            }

            if (intent == chat::ui::ChatComposeScreen::ActionIntent::Position)
            {
                std::string label = g_contacts_state.compose_screen->getText();
                gps::GpsState gps_state = gps::gps_get_data();
                if (!gps_state.valid)
                {
                    ::ui::SystemNotification::show("No GPS fix", 2000);
                    return;
                }

                team::proto::TeamChatLocation loc;
                loc.lat_e7 = static_cast<int32_t>(gps_state.lat * 1e7);
                loc.lon_e7 = static_cast<int32_t>(gps_state.lng * 1e7);
                if (gps_state.has_alt)
                {
                    double alt = gps_state.alt_m;
                    if (alt > 32767.0) alt = 32767.0;
                    if (alt < -32768.0) alt = -32768.0;
                    loc.alt_m = static_cast<int16_t>(lround(alt));
                }
                loc.ts = ts;
                if (!label.empty())
                {
                    loc.label = label;
                }

                std::vector<uint8_t> payload;
                if (!team::proto::encodeTeamChatLocation(loc, payload))
                {
                    ::ui::SystemNotification::show("Team location encode failed", 2000);
                    close_chat_compose();
                    return;
                }

                team::proto::TeamChatMessage msg;
                msg.header.type = team::proto::TeamChatType::Location;
                msg.header.ts = ts;
                msg.header.msg_id = s_team_msg_id++;
                if (s_team_msg_id == 0)
                {
                    s_team_msg_id = 1;
                }
                msg.payload = payload;

                bool ok = controller->onChat(msg, chat::ChannelId::PRIMARY);
                if (ok)
                {
                    team::ui::team_ui_chatlog_append_structured(
                        team::ui::g_team_state.team_id,
                        0,
                        false,
                        ts,
                        team::proto::TeamChatType::Location,
                        payload);
                }
                else
                {
                    ::ui::SystemNotification::show("Team chat send failed", 2000);
                }
            }
            else
            {
                std::string text = g_contacts_state.compose_screen->getText();
                if (text.empty())
                {
                    close_chat_compose();
                    return;
                }
                team::proto::TeamChatMessage msg;
                msg.header.type = team::proto::TeamChatType::Text;
                msg.header.ts = ts;
                msg.header.msg_id = s_team_msg_id++;
                if (s_team_msg_id == 0)
                {
                    s_team_msg_id = 1;
                }
                msg.payload.assign(text.begin(), text.end());

                bool ok = controller->onChat(msg, chat::ChannelId::PRIMARY);
                if (ok)
                {
                    team::ui::team_ui_chatlog_append_structured(
                        team::ui::g_team_state.team_id,
                        0,
                        false,
                        ts,
                        team::proto::TeamChatType::Text,
                        msg.payload);
                }
                else
                {
                    ::ui::SystemNotification::show("Team chat send failed", 2000);
                }
            }
            close_chat_compose();
            if (g_contacts_state.conversation_screen)
            {
                refresh_team_conversation();
            }
            return;
        }

        if (s_compose_protocol != active_mesh_protocol())
        {
            ::ui::SystemNotification::show("Conversation protocol mismatch", 2000);
            close_chat_compose();
            return;
        }

        std::string text = g_contacts_state.compose_screen->getText();
        if (!text.empty())
        {
            if (g_contacts_state.chat_service)
            {
                s_last_sent_text = text;
                s_last_sent_ts = static_cast<uint32_t>(time(nullptr));
                chat::MessageId msg_id = g_contacts_state.chat_service->sendText(
                    s_compose_channel, text, s_compose_peer_id);
                g_contacts_state.compose_screen->beginSend(
                    g_contacts_state.chat_service,
                    msg_id,
                    on_compose_send_done,
                    nullptr);
                return;
            }
        }
    }
    close_chat_compose();
}

static void on_compose_back(void* /*user_data*/)
{
    close_chat_compose();
}

static void on_compose_send_done(bool ok, bool /*timeout*/, void* /*user_data*/)
{
    if (ok && s_compose_is_team &&
        team::ui::g_team_state.has_team_id &&
        !s_last_sent_text.empty())
    {
        uint32_t ts = s_last_sent_ts;
        if (ts == 0)
        {
            ts = static_cast<uint32_t>(time(nullptr));
        }
        team::ui::team_ui_chatlog_append(team::ui::g_team_state.team_id,
                                         0,
                                         false,
                                         ts,
                                         s_last_sent_text);
    }
    close_chat_compose();
    s_last_sent_text.clear();
    s_last_sent_ts = 0;
    if (g_contacts_state.conversation_screen)
    {
        refresh_team_conversation();
    }
}

static void refresh_team_conversation()
{
    if (!g_contacts_state.conversation_screen || !is_team_available())
    {
        return;
    }
    g_contacts_state.conversation_screen->clearMessages();

    std::vector<team::ui::TeamChatLogEntry> entries;
    if (team::ui::team_ui_chatlog_load_recent(team::ui::g_team_state.team_id, 50, entries))
    {
        for (const auto& entry : entries)
        {
            chat::ChatMessage msg;
            msg.protocol = app::AppContext::getInstance().getConfig().mesh_protocol;
            msg.channel = chat::ChannelId::PRIMARY;
            msg.peer = 0;
            msg.from = entry.incoming ? entry.peer_id : 0;
            msg.timestamp = entry.ts;
            msg.text = format_team_chat_entry(entry);
            msg.status = entry.incoming ? chat::MessageStatus::Incoming : chat::MessageStatus::Sent;
            g_contacts_state.conversation_screen->addMessage(msg);
        }
    }
    g_contacts_state.conversation_screen->scrollToBottom();
}

static void on_team_conversation_action(chat::ui::ChatConversationScreen::ActionIntent intent, void* /*user_data*/)
{
    if (intent == chat::ui::ChatConversationScreen::ActionIntent::Reply)
    {
        s_compose_from_conversation = true;
        open_chat_compose();
    }
}

static void on_team_conversation_back(void* /*user_data*/)
{
    close_team_conversation();
}

static void open_team_conversation()
{
    if (g_contacts_state.conversation_screen)
    {
        return;
    }
    if (!is_team_available())
    {
        return;
    }

    lv_obj_t* parent = g_contacts_state.root
                           ? lv_obj_get_parent(g_contacts_state.root)
                           : lv_screen_active();
    if (lv_obj_t* chat_parent = ui_chat_get_container())
    {
        if (lv_obj_is_valid(chat_parent))
        {
            parent = chat_parent;
        }
    }

    s_conv_prev_group = lv_group_get_default();
    if (!s_conv_group)
    {
        s_conv_group = lv_group_create();
    }
    lv_group_remove_all_objs(s_conv_group);
    set_default_group(s_conv_group);

    const chat::MeshProtocol protocol = app::AppContext::getInstance().getConfig().mesh_protocol;
    chat::ConversationId conv(chat::ChannelId::PRIMARY, 0, protocol);
    g_contacts_state.conversation_screen = new chat::ui::ChatConversationScreen(parent, conv);
    g_contacts_state.conversation_screen->setActionCallback(on_team_conversation_action, nullptr);
    g_contacts_state.conversation_screen->setBackCallback(on_team_conversation_back, nullptr);

    const char* title = team::ui::g_team_state.team_name.empty()
                            ? "Team"
                            : team::ui::g_team_state.team_name.c_str();
    g_contacts_state.conversation_screen->setHeaderText(title, nullptr);
    g_contacts_state.conversation_screen->updateBatteryFromBoard();
    refresh_team_conversation();

    if (g_contacts_state.root)
    {
        lv_obj_add_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_contacts_state.refresh_timer)
    {
        lv_timer_pause(g_contacts_state.refresh_timer);
    }
    if (!g_contacts_state.conversation_timer)
    {
        g_contacts_state.conversation_timer = lv_timer_create([](lv_timer_t* timer)
                                                              {
            (void)timer;
            refresh_team_conversation(); },
                                                              1000, nullptr);
        lv_timer_set_repeat_count(g_contacts_state.conversation_timer, -1);
    }
    else
    {
        lv_timer_resume(g_contacts_state.conversation_timer);
    }
}

static void close_team_conversation()
{
    if (g_contacts_state.conversation_timer)
    {
        lv_timer_pause(g_contacts_state.conversation_timer);
    }
    if (g_contacts_state.conversation_screen)
    {
        delete g_contacts_state.conversation_screen;
        g_contacts_state.conversation_screen = nullptr;
    }

    if (g_contacts_state.root)
    {
        lv_obj_clear_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_contacts_state.refresh_timer)
    {
        lv_timer_resume(g_contacts_state.refresh_timer);
    }

    if (s_conv_prev_group)
    {
        set_default_group(s_conv_prev_group);
    }
    else if (lv_group_t* contacts_group = contacts_input_get_group())
    {
        set_default_group(contacts_group);
    }
    s_conv_prev_group = nullptr;
    contacts_focus_to_list();
    refresh_ui();
}

static void send_team_position()
{
    if (!is_team_available())
    {
        return;
    }
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (!controller)
    {
        return;
    }

    gps::GpsState gps_state = gps::gps_get_data();
    if (!gps_state.valid)
    {
        CONTACTS_LOG("[Contacts] team position: no gps fix\n");
        return;
    }

    int32_t lat_e7 = static_cast<int32_t>(gps_state.lat * 1e7);
    int32_t lon_e7 = static_cast<int32_t>(gps_state.lng * 1e7);
    uint32_t ts = static_cast<uint32_t>(time(nullptr));
    if (ts < 1577836800U)
    {
        ts = static_cast<uint32_t>(millis() / 1000);
    }

    team::proto::TeamPositionMessage pos{};
    pos.lat_e7 = lat_e7;
    pos.lon_e7 = lon_e7;
    pos.ts = ts;
    if (gps_state.has_alt)
    {
        double alt = gps_state.alt_m;
        if (alt > 32767.0) alt = 32767.0;
        else if (alt < -32768.0) alt = -32768.0;
        pos.alt_m = static_cast<int16_t>(lround(alt));
        pos.flags |= team::proto::kTeamPosHasAltitude;
    }
    if (gps_state.has_speed)
    {
        double dmps = gps_state.speed_mps * 10.0;
        if (dmps < 0.0) dmps = 0.0;
        if (dmps > 65535.0) dmps = 65535.0;
        pos.speed_dmps = static_cast<uint16_t>(lround(dmps));
        pos.flags |= team::proto::kTeamPosHasSpeed;
    }
    if (gps_state.has_course)
    {
        double course = gps_state.course_deg;
        if (course < 0.0) course = 0.0;
        uint32_t cdeg = static_cast<uint32_t>(lround(course * 100.0));
        if (cdeg >= 36000U) cdeg = 35999U;
        pos.course_cdeg = static_cast<uint16_t>(cdeg);
        pos.flags |= team::proto::kTeamPosHasCourse;
    }
    if (gps_state.satellites > 0)
    {
        pos.sats_in_view = static_cast<uint8_t>(
            gps_state.satellites > 255 ? 255 : gps_state.satellites);
        pos.flags |= team::proto::kTeamPosHasSatellites;
    }

    std::vector<uint8_t> payload;
    if (!team::proto::encodeTeamPositionMessage(pos, payload))
    {
        CONTACTS_LOG("[Contacts] team position: encode fail\n");
        return;
    }

    bool ok = controller->onPosition(payload, chat::ChannelId::PRIMARY);
    if (ok)
    {
        int16_t alt_m = gps_state.has_alt
                            ? static_cast<int16_t>(lround(gps_state.alt_m))
                            : 0;
        if (gps_state.has_alt)
        {
            if (gps_state.alt_m > 32767.0) alt_m = 32767;
            else if (gps_state.alt_m < -32768.0) alt_m = -32768;
        }
        uint16_t speed_dmps = 0;
        if (gps_state.has_speed)
        {
            double dmps = gps_state.speed_mps * 10.0;
            if (dmps < 0.0) dmps = 0.0;
            if (dmps > 65535.0) dmps = 65535.0;
            speed_dmps = static_cast<uint16_t>(lround(dmps));
        }
        team::ui::team_ui_posring_append(team::ui::g_team_state.team_id,
                                         0,
                                         lat_e7,
                                         lon_e7,
                                         alt_m,
                                         speed_dmps,
                                         ts);
    }
}

static void on_add_edit_save_clicked(lv_event_t* /*e*/)
{
    if (!g_contacts_state.add_edit_textarea || !g_contacts_state.add_edit_error_label)
    {
        return;
    }

    const char* nickname = lv_textarea_get_text(g_contacts_state.add_edit_textarea);
    if (!nickname || strlen(nickname) == 0)
    {
        lv_label_set_text(g_contacts_state.add_edit_error_label, "Name required");
        lv_obj_clear_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (strlen(nickname) > 12)
    {
        lv_label_set_text(g_contacts_state.add_edit_error_label, "Name too long");
        lv_obj_clear_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!g_contacts_state.contact_service)
    {
        return;
    }
    auto contacts = g_contacts_state.contact_service->getContacts();
    for (const auto& c : contacts)
    {
        if (c.node_id == g_contacts_state.modal_node_id)
        {
            continue;
        }
        if (c.display_name == nickname)
        {
            lv_label_set_text(g_contacts_state.add_edit_error_label, "Duplicate name not allowed");
            lv_obj_clear_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);
            return;
        }
    }

    bool ok = false;
    if (g_contacts_state.modal_is_edit)
    {
        ok = g_contacts_state.contact_service->editContact(g_contacts_state.modal_node_id, nickname);
    }
    else
    {
        ok = g_contacts_state.contact_service->addContact(g_contacts_state.modal_node_id, nickname);
    }

    if (!ok)
    {
        lv_label_set_text(g_contacts_state.add_edit_error_label, "Save failed");
        lv_obj_clear_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    g_contacts_state.add_edit_textarea = nullptr;
    g_contacts_state.add_edit_error_label = nullptr;
    modal_close(g_contacts_state.add_edit_modal);

    if (!g_contacts_state.modal_is_edit)
    {
        g_contacts_state.current_mode = ContactsMode::Contacts;
        g_contacts_state.current_page = 0;
    }
    g_contacts_state.selected_index = -1;
    refresh_contacts_data();
    refresh_ui();
    contacts_focus_to_list();
}

static void on_add_edit_cancel_clicked(lv_event_t* /*e*/)
{
    g_contacts_state.add_edit_textarea = nullptr;
    g_contacts_state.add_edit_error_label = nullptr;
    modal_close(g_contacts_state.add_edit_modal);
    contacts_focus_to_list();
}

static void on_del_confirm_clicked(lv_event_t* /*e*/)
{
    if (g_contacts_state.contact_service)
    {
        g_contacts_state.contact_service->removeContact(g_contacts_state.modal_node_id);
    }

    modal_close(g_contacts_state.del_confirm_modal);
    g_contacts_state.selected_index = -1;
    refresh_contacts_data();
    refresh_ui();
    contacts_focus_to_list();
}

static void on_del_cancel_clicked(lv_event_t* /*e*/)
{
    modal_close(g_contacts_state.del_confirm_modal);
    contacts_focus_to_list();
}

static void on_node_info_back_clicked(lv_event_t* /*e*/)
{
    close_node_info_screen();
}

static void execute_discovery_command(uint8_t command_index)
{
    DiscoveryActionSpec spec{};
    if (!get_discovery_action_spec(static_cast<int>(command_index), &spec))
    {
        return;
    }

    if (spec.command == DiscoveryActionCommand::Cancel)
    {
        ContactsMode fallback = g_contacts_state.last_action_mode;
        if (fallback == ContactsMode::Discover)
        {
            fallback = ContactsMode::Contacts;
        }
        g_contacts_state.current_mode = fallback;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
        refresh_ui();
        contacts_focus_to_filter();
        return;
    }

    if (active_mesh_protocol() != chat::MeshProtocol::MeshCore || !g_contacts_state.chat_service)
    {
        ::ui::SystemNotification::show("MeshCore only", 2000);
        return;
    }

    if (spec.command == DiscoveryActionCommand::ScanLocal)
    {
        refresh_contacts_data();
        g_contacts_state.discover_scan_start_nearby = g_contacts_state.nearby_list.size();
        if (g_contacts_state.discover_scan_timer)
        {
            lv_timer_del(g_contacts_state.discover_scan_timer);
            g_contacts_state.discover_scan_timer = nullptr;
        }
        const bool ok = g_contacts_state.chat_service->triggerDiscoveryAction(chat::MeshDiscoveryAction::ScanLocal);
        if (!ok)
        {
            ::ui::SystemNotification::show("Scan failed", 2000);
            return;
        }
        ::ui::SystemNotification::show("Scanning 5s...", 1800);
        g_contacts_state.discover_scan_timer = lv_timer_create(on_discovery_scan_done, 5000, nullptr);
        lv_timer_set_repeat_count(g_contacts_state.discover_scan_timer, 1);
        return;
    }

    const chat::MeshDiscoveryAction action =
        (spec.command == DiscoveryActionCommand::SendIdLocal)
            ? chat::MeshDiscoveryAction::SendIdLocal
            : chat::MeshDiscoveryAction::SendIdBroadcast;
    const bool ok = g_contacts_state.chat_service->triggerDiscoveryAction(action);
    if (ok)
    {
        ::ui::SystemNotification::show(
            (spec.command == DiscoveryActionCommand::SendIdLocal) ? "ID local sent" : "ID bcast sent",
            2000);
    }
    else
    {
        ::ui::SystemNotification::show(
            (spec.command == DiscoveryActionCommand::SendIdLocal) ? "ID local fail" : "ID bcast fail",
            2000);
    }
}

static void on_discovery_scan_done(lv_timer_t* timer)
{
    const size_t start_count = g_contacts_state.discover_scan_start_nearby;
    refresh_contacts_data();
    refresh_ui();
    const size_t total = g_contacts_state.nearby_list.size();
    const size_t gained = (total > start_count) ? (total - start_count) : 0;

    char msg[24] = {};
    snprintf(msg, sizeof(msg), "Scan +%u/%u",
             static_cast<unsigned>(gained),
             static_cast<unsigned>(total));
    ::ui::SystemNotification::show(msg, 2200);

    if (timer)
    {
        lv_timer_del(timer);
    }
    if (timer == g_contacts_state.discover_scan_timer)
    {
        g_contacts_state.discover_scan_timer = nullptr;
    }
}

enum class ActionMenuCommand : uint8_t
{
    Chat = 1,
    Position = 2,
    Info = 3,
    Edit = 4,
    Add = 5,
    Delete = 6,
    Cancel = 7,
};

static lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), kButtonHeight);
    contacts::ui::style::apply_btn_basic(btn);
    // Keep default neutral background and highlight strictly by focus state.
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorPanelBg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorAmber), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorAmber), LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorAmberDark), LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    apply_primary_text(label);
    lv_obj_center(label);
    return btn;
}

static void on_action_menu_key(lv_event_t* e)
{
    uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        modal_close(g_contacts_state.action_menu_modal);
        contacts_focus_to_list();
    }
}

static void on_action_menu_item_clicked(lv_event_t* e)
{
    ActionMenuCommand cmd = static_cast<ActionMenuCommand>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));

    modal_close(g_contacts_state.action_menu_modal);
    contacts_focus_to_list();

    switch (cmd)
    {
    case ActionMenuCommand::Chat:
        open_chat_compose();
        break;
    case ActionMenuCommand::Position:
        send_team_position();
        break;
    case ActionMenuCommand::Info:
        open_node_info_screen();
        break;
    case ActionMenuCommand::Edit:
        open_add_edit_modal(true);
        break;
    case ActionMenuCommand::Add:
        open_add_edit_modal(false);
        break;
    case ActionMenuCommand::Delete:
        open_delete_confirm_modal();
        break;
    case ActionMenuCommand::Cancel:
    default:
        break;
    }
}

static void open_action_menu_modal()
{
    if (is_any_modal_open())
    {
        return;
    }
    if (g_contacts_state.current_mode == ContactsMode::Discover)
    {
        return;
    }
    if (g_contacts_state.selected_index < 0)
    {
        return;
    }

    int action_count = 2; // Chat + Cancel
    if (g_contacts_state.current_mode == ContactsMode::Contacts)
    {
        action_count += 3; // Edit/Delete/Info
    }
    else if (g_contacts_state.current_mode == ContactsMode::Nearby)
    {
        action_count += 2; // Add/Info
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        action_count += 1; // Position
    }

    int modal_h = 62 + action_count * (kButtonHeight + 2);
    if (modal_h > 216)
    {
        modal_h = 216;
    }

    modal_prepare_group();
    g_contacts_state.action_menu_modal = create_modal_root(190, modal_h);
    lv_obj_t* win = lv_obj_get_child(g_contacts_state.action_menu_modal, 0);
    if (!win)
    {
        modal_close(g_contacts_state.action_menu_modal);
        return;
    }

    std::string title = "Actions";
    if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        title = "Team Actions";
    }
    else if (g_contacts_state.current_mode == ContactsMode::Broadcast)
    {
        title = "Channel Actions";
    }
    else if (const auto* node = get_selected_node())
    {
        std::string name = node->display_name;
        if (name.empty())
        {
            name = node->short_name;
        }
        if (!name.empty())
        {
            title = "Actions: " + name;
        }
    }

    // Use flex layout to avoid stale height reads causing a too-short action list
    // on some devices (e.g. T-Deck). Let the list container grow to fill window.
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(win, 4, LV_PART_MAIN);

    lv_obj_t* title_label = lv_label_create(win);
    lv_label_set_text(title_label, title.c_str());
    apply_primary_text(title_label);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_margin_bottom(title_label, 2, LV_PART_MAIN);

    lv_obj_t* list = lv_obj_create(win);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_height(list, 0);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 2, LV_PART_MAIN);
    lv_obj_set_style_min_height(list, kButtonHeight + 4, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* first_focus = nullptr;
    auto add_action = [&](ActionMenuCommand cmd, const char* text)
    {
        lv_obj_t* btn = create_action_menu_button(list, text);
        lv_obj_add_event_cb(
            btn,
            on_action_menu_item_clicked,
            LV_EVENT_CLICKED,
            reinterpret_cast<void*>(static_cast<uintptr_t>(cmd)));
        lv_obj_add_event_cb(btn, on_action_menu_key, LV_EVENT_KEY, nullptr);
        lv_group_add_obj(g_contacts_state.modal_group, btn);
        if (!first_focus)
        {
            first_focus = btn;
        }
    };

    add_action(ActionMenuCommand::Chat, "Chat");
    if (g_contacts_state.current_mode == ContactsMode::Contacts)
    {
        add_action(ActionMenuCommand::Edit, "Edit");
        add_action(ActionMenuCommand::Delete, "Delete");
        add_action(ActionMenuCommand::Info, "Info");
    }
    else if (g_contacts_state.current_mode == ContactsMode::Nearby)
    {
        add_action(ActionMenuCommand::Add, "Add");
        add_action(ActionMenuCommand::Info, "Info");
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        add_action(ActionMenuCommand::Position, "Position");
    }
    add_action(ActionMenuCommand::Cancel, "Cancel");

    if (first_focus)
    {
        lv_group_focus_obj(first_focus);
    }
}

// ---------------- UI refresh (public API) ----------------

void refresh_ui()
{
    if (g_contacts_state.list_panel == nullptr)
    {
        return;
    }
    if (s_refreshing_ui)
    {
        return;
    }
    s_refreshing_ui = true;

    lv_obj_t* active = lv_screen_active();
    if (!active)
    {
        CONTACTS_LOG("[Contacts] WARNING: lv_screen_active() is null\n");
    }
    else
    {
        CONTACTS_LOG("[Contacts] refresh_ui: active=%p root=%p list_panel=%p\n",
                     active, g_contacts_state.root, g_contacts_state.list_panel);
    }
    if (g_contacts_state.root && !lv_obj_is_valid(g_contacts_state.root))
    {
        CONTACTS_LOG("[Contacts] WARNING: root is invalid\n");
    }
    if (g_contacts_state.list_panel && !lv_obj_is_valid(g_contacts_state.list_panel))
    {
        CONTACTS_LOG("[Contacts] WARNING: list_panel is invalid\n");
    }

    lv_obj_clear_flag(g_contacts_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    if (g_contacts_state.sub_container)
    {
        lv_obj_clear_flag(g_contacts_state.sub_container, LV_OBJ_FLAG_SCROLLABLE);
    }

    bool team_available = is_team_available();
    const bool meshcore_mode = (active_mesh_protocol() == chat::MeshProtocol::MeshCore);
    if (g_contacts_state.team_btn)
    {
        if (team_available)
        {
            lv_obj_clear_flag(g_contacts_state.team_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_contacts_state.team_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_contacts_state.discover_btn)
    {
        if (meshcore_mode)
        {
            lv_obj_clear_flag(g_contacts_state.discover_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_contacts_state.discover_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!team_available && g_contacts_state.current_mode == ContactsMode::Team)
    {
        g_contacts_state.current_mode = ContactsMode::Contacts;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
    }
    if (!meshcore_mode && g_contacts_state.current_mode == ContactsMode::Discover)
    {
        g_contacts_state.current_mode = ContactsMode::Contacts;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
    }

    // Log nearby nodes if in nearby mode (unchanged)
    if (g_contacts_state.current_mode == ContactsMode::Nearby)
    {
        CONTACTS_LOG("[Contacts] Nearby mode: %zu nodes\n", g_contacts_state.nearby_list.size());
        for (size_t i = 0; i < g_contacts_state.nearby_list.size(); ++i)
        {
            const auto& node = g_contacts_state.nearby_list[i];
            CONTACTS_LOG("  Node %zu: %s (last_seen=%u, snr=%.1f)\n",
                         i, node.display_name.c_str(), node.last_seen, node.snr);
        }
    }

    // Ensure list containers exist (structure handled in layout)
    contacts::ui::layout::ensure_list_subcontainers();

    // Clear existing list items (unchanged)
    for (auto* item : g_contacts_state.list_items)
    {
        if (item != nullptr)
        {
            lv_obj_del(item);
        }
    }
    g_contacts_state.list_items.clear();

    // Choose list by mode (unchanged)
    std::vector<chat::contacts::NodeInfo> broadcast_list;
    std::vector<chat::contacts::NodeInfo> team_list;
    std::vector<chat::contacts::NodeInfo> discover_list;
    const std::vector<chat::contacts::NodeInfo>* current_list = nullptr;
    if (g_contacts_state.current_mode == ContactsMode::Contacts)
    {
        current_list = &g_contacts_state.contacts_list;
    }
    else if (g_contacts_state.current_mode == ContactsMode::Nearby)
    {
        current_list = &g_contacts_state.nearby_list;
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        chat::contacts::NodeInfo team_node{};
        team_node.node_id = 0;
        team_node.last_seen = 0;
        team_node.snr = 0.0f;
        team_node.is_contact = false;
        team_node.protocol = chat::contacts::NodeProtocolType::Unknown;
        team_node.display_name = team::ui::g_team_state.team_name.empty()
                                     ? "Team"
                                     : team::ui::g_team_state.team_name;
        team_list.push_back(team_node);
        current_list = &team_list;
    }
    else if (g_contacts_state.current_mode == ContactsMode::Discover)
    {
        for (size_t i = 0; i < (sizeof(kDiscoveryActionSpecs) / sizeof(kDiscoveryActionSpecs[0])); ++i)
        {
            chat::contacts::NodeInfo item{};
            item.node_id = static_cast<uint32_t>(i + 1);
            item.display_name = kDiscoveryActionSpecs[i].label;
            item.protocol = chat::contacts::NodeProtocolType::MeshCore;
            discover_list.push_back(item);
        }
        current_list = &discover_list;
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            BroadcastTargetSpec spec{};
            if (!get_broadcast_target_spec(i, &spec))
            {
                continue;
            }
            chat::contacts::NodeInfo target{};
            target.display_name = spec.label;
            target.protocol = (spec.protocol == chat::MeshProtocol::MeshCore)
                                  ? chat::contacts::NodeProtocolType::MeshCore
                                  : chat::contacts::NodeProtocolType::Meshtastic;
            broadcast_list.push_back(target);
        }
        current_list = &broadcast_list;
    }

    g_contacts_state.total_items = current_list->size();

    if (g_contacts_state.selected_index >= static_cast<int>(g_contacts_state.total_items))
    {
        g_contacts_state.selected_index = -1;
    }

    // Pagination calc (unchanged)
    int total_pages = (static_cast<int>(g_contacts_state.total_items) + kItemsPerPage - 1) / kItemsPerPage;
    if (total_pages == 0) total_pages = 1;

    if (g_contacts_state.current_page >= total_pages)
    {
        g_contacts_state.current_page = total_pages - 1;
    }
    if (g_contacts_state.current_page < 0)
    {
        g_contacts_state.current_page = 0;
    }

    int start_idx = g_contacts_state.current_page * kItemsPerPage;
    int end_idx = start_idx + kItemsPerPage;
    if (end_idx > static_cast<int>(g_contacts_state.total_items))
    {
        end_idx = static_cast<int>(g_contacts_state.total_items);
    }

    // Create list items for current page (structure in layout; status string computed here)
    for (int i = start_idx; i < end_idx; ++i)
    {
        const auto& node = (*current_list)[i];

        std::string status_text;
        if (g_contacts_state.current_mode == ContactsMode::Contacts)
        {
            status_text = format_time_status(node.last_seen);
            const char* proto = node_protocol_short_label(node.protocol);
            if (proto[0] != '\0')
            {
                status_text += " ";
                status_text += proto;
            }
        }
        else if (g_contacts_state.current_mode == ContactsMode::Nearby)
        {
            status_text = format_time_status(node.last_seen);
            const char* proto = node_protocol_short_label(node.protocol);
            if (proto[0] != '\0')
            {
                status_text += " ";
                status_text += proto;
            }
        }
        else if (g_contacts_state.current_mode == ContactsMode::Team)
        {
            status_text = "Team";
        }
        else if (g_contacts_state.current_mode == ContactsMode::Discover)
        {
            DiscoveryActionSpec spec{};
            if (get_discovery_action_spec(i, &spec))
            {
                status_text = spec.status;
            }
            else
            {
                status_text = "Action";
            }
        }
        else
        {
            BroadcastTargetSpec spec{};
            if (get_broadcast_target_spec(i, &spec))
            {
                status_text = (spec.protocol == active_mesh_protocol()) ? "Ready" : "Switch";
            }
            else
            {
                status_text = "Channel";
            }
        }

        lv_obj_t* item = contacts::ui::layout::create_list_item(
            g_contacts_state.sub_container,
            node,
            g_contacts_state.current_mode,
            status_text.c_str());

        // 让 item 记录它对应的全局 index，按压时能知道选中了谁
        lv_obj_set_user_data(item, (void*)(intptr_t)i);

        // 按压列表行：弹出动作菜单
        lv_obj_add_event_cb(item, on_list_item_clicked, LV_EVENT_CLICKED, nullptr);
    }

    // Create bottom buttons (create once; width follows label text)
    if (g_contacts_state.next_btn == nullptr)
    {
        g_contacts_state.next_btn = create_bottom_bar_button(
            g_contacts_state.bottom_container,
            "Next",
            kColorAmber,
            on_next_clicked);
    }

    if (g_contacts_state.prev_btn == nullptr)
    {
        g_contacts_state.prev_btn = create_bottom_bar_button(
            g_contacts_state.bottom_container,
            "Prev",
            kColorPanelBg,
            on_prev_clicked);
    }

    if (g_contacts_state.back_btn == nullptr)
    {
        g_contacts_state.back_btn = create_bottom_bar_button(
            g_contacts_state.bottom_container,
            "Back",
            kColorAmber,
            on_back_clicked);
    }

    // Enable/disable buttons based on pagination (unchanged from original)
    if (total_pages > 1)
    {
        lv_obj_clear_state(g_contacts_state.prev_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(g_contacts_state.next_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(g_contacts_state.prev_btn, LV_STATE_DISABLED);
        lv_obj_add_state(g_contacts_state.next_btn, LV_STATE_DISABLED);
    }
    lv_obj_clear_state(g_contacts_state.back_btn, LV_STATE_DISABLED);

    // Update filter highlights (visual-only, using CHECKED state).
    refresh_filter_checked_state();

    if (g_contacts_state.list_panel)
    {
        lv_obj_scroll_to_y(g_contacts_state.list_panel, 0, LV_ANIM_OFF);
        lv_obj_invalidate(g_contacts_state.list_panel);
        lv_obj_add_flag(g_contacts_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(g_contacts_state.list_panel, LV_SCROLLBAR_MODE_AUTO);
    }
    if (g_contacts_state.sub_container)
    {
        lv_obj_add_flag(g_contacts_state.sub_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(g_contacts_state.sub_container, LV_SCROLLBAR_MODE_OFF);
    }
    s_refreshing_ui = false;
    contacts_input_on_ui_refreshed();
}

// ---------------- Modal cleanup (public API) ----------------

void cleanup_modals()
{
    if (g_contacts_state.add_edit_modal != nullptr)
    {
        lv_obj_del(g_contacts_state.add_edit_modal);
        g_contacts_state.add_edit_modal = nullptr;
    }
    g_contacts_state.add_edit_textarea = nullptr;
    g_contacts_state.add_edit_error_label = nullptr;
    if (g_contacts_state.del_confirm_modal != nullptr)
    {
        lv_obj_del(g_contacts_state.del_confirm_modal);
        g_contacts_state.del_confirm_modal = nullptr;
    }
    if (g_contacts_state.action_menu_modal != nullptr)
    {
        lv_obj_del(g_contacts_state.action_menu_modal);
        g_contacts_state.action_menu_modal = nullptr;
    }
    if (g_contacts_state.discover_modal != nullptr)
    {
        lv_obj_del(g_contacts_state.discover_modal);
        g_contacts_state.discover_modal = nullptr;
    }
    if (g_contacts_state.discover_scan_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.discover_scan_timer);
        g_contacts_state.discover_scan_timer = nullptr;
    }
    if (g_contacts_state.node_info_root != nullptr)
    {
        node_info::ui::destroy();
        g_contacts_state.node_info_root = nullptr;
    }
    if (g_contacts_state.node_info_group != nullptr)
    {
        lv_group_del(g_contacts_state.node_info_group);
        g_contacts_state.node_info_group = nullptr;
    }
    g_contacts_state.node_info_prev_group = nullptr;
    if (g_contacts_state.modal_group != nullptr)
    {
        lv_group_del(g_contacts_state.modal_group);
        g_contacts_state.modal_group = nullptr;
    }
    g_contacts_state.prev_group = nullptr;
}
