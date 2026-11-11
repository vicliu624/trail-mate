/**
 * @file contacts_page_components.cpp
 * @brief Contacts page behavior implementation on top of shared layout/styles
 */

#include "ui/screens/contacts/contacts_page_components.h"
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/reticulum_identity.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshtastic/mt_radio_config.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "sys/clock.h"
#include "team/protocol/team_position.h"
#include "team/usecase/team_controller.h"
#include "ui/app_runtime.h"
#include "ui/components/bottom_bar.h"
#include "ui/components/floating_search_box.h"
#include "ui/components/info_card.h"
#include "ui/components/shortcut_help_modal.h"
#include "ui/input/cardputer_softkeys.h"
#include "ui/localization.h"
#include "ui/menu/menu_layout.h"
#include "ui/page/page_profile.h"
#include "ui/presentation_sources/team_chat_presentation_source.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/screens/chat/chat_conversation_components.h"
#include "ui/screens/chat/chat_page_runtime.h"
#include "ui/screens/chat/chat_page_shell.h"
#include "ui/screens/chat/chat_protocol_support.h"
#include "ui/screens/contacts/contacts_filter_profile.h"
#include "ui/screens/contacts/contacts_node_card_view.h"
#include "ui/screens/contacts/contacts_page_input.h"
#include "ui/screens/contacts/contacts_page_layout.h"
#include "ui/screens/contacts/contacts_page_styles.h"
#include "ui/screens/contacts/contacts_state.h"
#include "ui/screens/contacts/contacts_team_snapshot_source.h"
#include "ui/screens/node_info/node_info_page_components.h"
#include "ui/team_actions/team_action_runtime_sink.h"
#include "ui/team_actions/team_runtime_adapters.h"
#include "ui/ui_common.h"
#include "ui/widgets/busy_overlay.h"
#include "ui/widgets/reticulum_ping_overlay.h"
#include "ui/widgets/top_bar.h"
#include "ui_lvgl_ux_packs/common/touch_text_editor.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#define CONTACTS_DEBUG 0
#if CONTACTS_DEBUG
#define CONTACTS_LOG(...) std::printf(__VA_ARGS__)
#else
#define CONTACTS_LOG(...)
#endif

#define CONTACTS_NODE_INFO_LOG(...) std::printf("[Contacts][NodeInfo] " __VA_ARGS__)

using namespace contacts::ui;
namespace chat_support = chat::ui::support;
namespace rtdir = ::platform::ui::reticulum_directory;

static constexpr int kItemsPerPage = 4;
static constexpr intptr_t kBackListItemUserData = -2;
static constexpr intptr_t kAddReticulumGroupUserData = -3;

// UI color tokens (must align with docs/skyplot.md)
static constexpr uint32_t kColorAmber = 0xEBA341;
static constexpr uint32_t kColorAmberDark = 0xC98118;
static constexpr uint32_t kColorPanelBg = 0xFAF0D8;
static constexpr uint32_t kColorLine = 0xE7C98F;
static constexpr uint32_t kColorText = 0x6B4A1E;
static constexpr uint32_t kColorWarn = 0xB94A2C;

static bool s_refreshing_ui = false;
static lv_coord_t page_button_height()
{
    return ::ui::page_profile::resolve_control_button_height();
}

static lv_group_t* s_conv_group = nullptr;
static lv_group_t* s_conv_prev_group = nullptr;
static std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort> s_team_chat_command_port;
static std::unique_ptr<::ui::team_actions::ITeamLocationSource> s_team_location_source;
static std::unique_ptr<::ui::team_actions::TeamActionRuntimeSink> s_team_action_sink;
static std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource> s_team_chat_source;
static team::TeamController* s_team_action_controller = nullptr;
static std::unique_ptr<contacts::ui::ContactsTeamSnapshotSource> s_team_snapshot_source;
static ::ui::components::shortcut_help_modal::State s_help_modal{};
static ::ui::widgets::TopBar s_reticulum_node_info_top_bar{};

static void format_reticulum_hash_prefix(const chat::ReticulumPeerIdentity& identity,
                                         char* out,
                                         size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!chat::hasReticulumDestinationIdentity(identity) || out_len < 9)
    {
        std::snprintf(out, out_len, "-");
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%02X%02X%02X%02X",
                  static_cast<unsigned>(identity.destination_hash[0]),
                  static_cast<unsigned>(identity.destination_hash[1]),
                  static_cast<unsigned>(identity.destination_hash[2]),
                  static_cast<unsigned>(identity.destination_hash[3]));
}

static void format_reticulum_hash_text(const uint8_t* hash, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || out_len < (chat::kReticulumPeerHashSize * 2U + 1U))
    {
        std::snprintf(out, out_len, "--");
        return;
    }
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        out[index * 2U] =
            chat::reticulumHexDigit(static_cast<uint8_t>(hash[index] >> 4U));
        out[index * 2U + 1U] = chat::reticulumHexDigit(hash[index]);
    }
    out[chat::kReticulumPeerHashSize * 2U] = '\0';
}

static bool is_reticulum_node(const chat::contacts::PeerDirectoryItem& node)
{
    return node.protocol == chat::contacts::NodeProtocolType::Reticulum ||
           chat::hasReticulumDestinationIdentity(node.reticulum_identity);
}

static std::string node_display_name_for_contacts(const chat::contacts::PeerDirectoryItem& node)
{
    return contacts::ui::layout::preferred_node_display_name(node);
}

static bool show_second_column_back()
{
    return !::ui::components::info_card::use_tdeck_layout();
}

static bool filter_mode_for_button(lv_obj_t* target, ContactsMode* out_mode)
{
    if (!target || !out_mode)
    {
        return false;
    }
    if (target == g_contacts_state.contacts_btn)
    {
        *out_mode = ContactsMode::Contacts;
        return true;
    }
    if (target == g_contacts_state.nearby_btn)
    {
        *out_mode = ContactsMode::Nearby;
        return true;
    }
    if (target == g_contacts_state.groups_btn)
    {
        *out_mode = ContactsMode::Groups;
        return true;
    }
    if (target == g_contacts_state.ignored_btn)
    {
        *out_mode = ContactsMode::Ignored;
        return true;
    }
    if (target == g_contacts_state.broadcast_btn)
    {
        *out_mode = ContactsMode::Broadcast;
        return true;
    }
    if (target == g_contacts_state.team_btn)
    {
        *out_mode = ContactsMode::Team;
        return true;
    }
    if (target == g_contacts_state.discover_btn)
    {
        *out_mode = ContactsMode::Discover;
        return true;
    }
    return false;
}

lv_style_selector_t selector_for_state(lv_state_t state)
{
    return static_cast<lv_style_selector_t>(static_cast<unsigned>(LV_PART_MAIN) | static_cast<unsigned>(state));
}

// --- Forward declarations (same behavior as original) ---
static std::string format_time_status(uint32_t last_seen);
[[maybe_unused]] static std::string format_snr(float snr);

static void on_filter_focused(lv_event_t* e);
static void on_list_item_clicked(lv_event_t* e);
static void on_list_item_focused(lv_event_t* e);
static void on_prev_clicked(lv_event_t* e);
static void on_next_clicked(lv_event_t* e);
static void on_back_clicked(lv_event_t* e);
static void open_action_menu_modal();
static void on_action_menu_item_clicked(lv_event_t* e);
static void on_action_menu_key(lv_event_t* e);
static lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text);
static void open_search_modal();
static void open_lxmf_address_modal();
static void open_contacts_help_modal();
static void on_search_apply(const char* text, void* user_data);
static void on_search_clear(void* user_data);
static void on_search_cancel(void* user_data);
static void on_lxmf_address_apply(const char* text, void* user_data);
static void on_lxmf_address_clear(void* user_data);
static void on_lxmf_address_cancel(void* user_data);
static void apply_filter_panel_visibility();
static void toggle_filter_panel_visibility();
static void contacts_handle_page_shortcut(lv_event_t* event);
static const chat::contacts::PeerDirectoryItem* get_selected_node();
static const chat::contacts::PeerDirectoryItem* get_selected_reticulum_group();
static const chat::contacts::PeerDirectoryItem* find_node_by_id(uint32_t node_id);
struct BroadcastTargetSpec;
static bool get_selected_broadcast_target(BroadcastTargetSpec* out_spec,
                                          std::string* out_title);
static void open_add_edit_modal(bool is_edit);
static void open_reticulum_group_config_modal();
static void open_delete_confirm_modal();
static void open_node_info_screen_for_node(uint32_t node_id);
static void open_reticulum_node_info_screen(const chat::contacts::PeerDirectoryItem& node,
                                            lv_obj_t* parent);
static void close_node_info_screen();
static void modal_close(lv_obj_t*& modal_obj);
static void modal_prepare_group();
static void modal_restore_group();
static lv_obj_t* create_modal_root(int width, int height);
static bool is_any_modal_open();
static void on_add_edit_save_clicked(lv_event_t* e);
static void on_add_edit_cancel_clicked(lv_event_t* e);
static void on_reticulum_group_save_clicked(lv_event_t* e);
static void on_reticulum_group_cancel_clicked(lv_event_t* e);
static void on_del_confirm_clicked(lv_event_t* e);
static void on_del_cancel_clicked(lv_event_t* e);
static void on_discovery_scan_done(lv_timer_t* timer);
static void execute_discovery_command(uint8_t command_index);
static void on_node_info_key(lv_event_t* e);
static void open_chat_compose();
[[maybe_unused]] static void open_team_conversation();
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
        g_contacts_state.ignored_btn == nullptr)
    {
        return;
    }

    lv_obj_clear_state(g_contacts_state.contacts_btn, LV_STATE_CHECKED);
    lv_obj_clear_state(g_contacts_state.nearby_btn, LV_STATE_CHECKED);
    if (g_contacts_state.groups_btn)
    {
        lv_obj_clear_state(g_contacts_state.groups_btn, LV_STATE_CHECKED);
    }
    lv_obj_clear_state(g_contacts_state.ignored_btn, LV_STATE_CHECKED);
    if (g_contacts_state.broadcast_btn)
    {
        lv_obj_clear_state(g_contacts_state.broadcast_btn, LV_STATE_CHECKED);
    }
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
    else if (g_contacts_state.current_mode == ContactsMode::Ignored)
    {
        lv_obj_add_state(g_contacts_state.ignored_btn, LV_STATE_CHECKED);
    }
    else if (g_contacts_state.current_mode == ContactsMode::Groups &&
             g_contacts_state.groups_btn)
    {
        lv_obj_add_state(g_contacts_state.groups_btn, LV_STATE_CHECKED);
    }
    else if (g_contacts_state.current_mode == ContactsMode::Broadcast &&
             g_contacts_state.broadcast_btn)
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
    (void)bg_color;
    auto* button = lv_btn_create(parent);
    auto* label = lv_label_create(button);
    ::ui::i18n::set_label_text(label, text);
    ::ui::components::bottom_bar::style_button(button);
    lv_obj_set_style_pad_hor(button, 6, LV_PART_MAIN);
    lv_obj_set_size(button, LV_SIZE_CONTENT, ::ui::components::bottom_bar::kButtonHeight);
    lv_obj_center(label);
    if (cb) lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, nullptr);
    return button;
}

static void clear_footer_actions()
{
    auto& state = g_contacts_state;
    for (auto** handle : {&state.prev_btn, &state.next_btn, &state.back_btn, &state.card_details_btn})
    {
        if (*handle && lv_obj_is_valid(*handle)) lv_obj_del(*handle);
        *handle = nullptr;
    }
}

static void arrange_footer_actions()
{
    auto& state = g_contacts_state;
    int index = 0;
    const int slots[] = {0, 1, 2, 3};
    lv_obj_t* buttons[] = {state.prev_btn, state.next_btn, state.card_details_btn, state.back_btn};
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!buttons[i]) continue;
        lv_obj_move_to_index(buttons[i], index++);
        ::ui::input::cardputer::softkeys::bind_button(state.bottom_container, buttons[i], slots[i]);
    }
}

void refresh_contacts_data()
{
    refresh_contacts_data_impl();
}

// ---------------- Formatting helpers ----------------

static std::string format_time_status(uint32_t last_seen)
{
    uint32_t now_secs = sys::epoch_seconds_now();
    if (now_secs < last_seen)
    {
        return ::ui::i18n::tr("Offline");
    }

    uint32_t age_secs = now_secs - last_seen;

    // Online: 闂?2 minutes
    if (age_secs <= 120)
    {
        return ::ui::i18n::tr("Online");
    }

    // Minutes: 3-59 minutes
    if (age_secs < 3600)
    {
        uint32_t minutes = age_secs / 60;
        return ::ui::i18n::format("Seen %um", static_cast<unsigned>(minutes));
    }

    // Hours: 1-23 hours
    if (age_secs < 86400)
    {
        uint32_t hours = age_secs / 3600;
        return ::ui::i18n::format("Seen %uh", static_cast<unsigned>(hours));
    }

    // Days: 1-6 days
    if (age_secs < 6 * 86400)
    {
        uint32_t days = age_secs / 86400;
        return ::ui::i18n::format("Seen %ud", static_cast<unsigned>(days));
    }

    // > 6 days: should be filtered out
    return ::ui::i18n::tr("Offline");
}

static bool node_protocol_to_mesh(chat::contacts::NodeProtocolType protocol, chat::MeshProtocol* out)
{
    if (!out)
    {
        return false;
    }
    if (!chat::infra::isValidNodeProtocol(protocol) ||
        protocol == chat::contacts::NodeProtocolType::Unknown)
    {
        return false;
    }
    *out = chat::infra::meshProtocolFromNodeProtocol(protocol);
    return true;
}

struct BroadcastTargetSpec
{
    chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
    chat::ChannelId channel = chat::ChannelId::PRIMARY;
    uint8_t channel_index = 0;
    bool enabled = false;
    bool chat_supported = false;
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

static const char* broadcast_chat_unavailable_message(const BroadcastTargetSpec& spec)
{
    if (spec.protocol == chat::MeshProtocol::Meshtastic)
    {
        return "MT send uses slot 0/1 only";
    }
    if (chat::infra::isReticulumMeshProtocol(spec.protocol))
    {
        return "Reticulum group send unavailable";
    }
    if (!spec.enabled)
    {
        return "MC channel disabled";
    }
    return "MC channel key missing";
}

static size_t get_broadcast_target_count()
{
    switch (chat_support::active_mesh_protocol())
    {
    case chat::MeshProtocol::Meshtastic:
        return 8U;
    case chat::MeshProtocol::MeshCore:
        return chat::kMeshCoreChannelMaxCount;
    case chat::MeshProtocol::Reticulum:
        return 1U;
    default:
        return 0U;
    }
}

static bool get_broadcast_target_spec(int index, BroadcastTargetSpec* out)
{
    if (!out || index < 0)
    {
        return false;
    }

    if (chat_support::active_mesh_protocol() == chat::MeshProtocol::Meshtastic)
    {
        if (index >= 8)
        {
            return false;
        }

        const auto& cfg = app::appFacade().readConfig();
        out->protocol = chat::MeshProtocol::Meshtastic;
        out->channel_index = static_cast<uint8_t>(index);
        out->channel = (index == 1) ? chat::ChannelId::SECONDARY : chat::ChannelId::PRIMARY;
        out->enabled = (index == 0) ? cfg.primary_enabled : ((index == 1) ? cfg.secondary_enabled : false);
        out->chat_supported = out->enabled && (index <= 1);
        return true;
    }

    if (chat::infra::isReticulumMeshProtocol(chat_support::active_mesh_protocol()))
    {
        if (index != 0)
        {
            return false;
        }
        out->protocol = chat::MeshProtocol::Reticulum;
        out->channel = chat::ChannelId::PRIMARY;
        out->channel_index = 0;
        out->enabled = true;
        out->chat_supported = false;
        return true;
    }

    if (index >= static_cast<int>(chat::kMeshCoreChannelMaxCount))
    {
        return false;
    }
    const auto& cfg = app::appFacade().readConfig().meshcore_config;
    const uint8_t slot = static_cast<uint8_t>(index);
    const chat::MeshCoreChannelConfig& channel = cfg.meshCoreChannel(slot);
    const bool has_key =
        !chat::isAllZeroKeyBytes(channel.key, chat::kMeshCoreChannelKeyLen);
    out->protocol = chat::MeshProtocol::MeshCore;
    out->channel = chat::meshCoreChannelIdFromSlot(slot);
    out->channel_index = slot;
    out->enabled = (slot == 0) ? true : channel.enabled;
    out->chat_supported = out->enabled && (slot == 0 || has_key);
    return true;
}

static std::string format_broadcast_target_label(const BroadcastTargetSpec& spec)
{
    if (spec.protocol == chat::MeshProtocol::Meshtastic)
    {
        return chat::meshtastic::channelName(app::appFacade().readConfig().meshtastic_config,
                                             spec.channel);
    }
    if (chat::infra::isReticulumMeshProtocol(spec.protocol))
    {
        return ::ui::i18n::tr("Primary Group");
    }
    const chat::MeshConfig& cfg = app::appFacade().readConfig().meshcore_config;
    const chat::MeshCoreChannelConfig& channel =
        cfg.meshCoreChannel(spec.channel_index);
    const char* name = channel.name[0] != '\0' ? channel.name : nullptr;
    char fallback[16] = {};
    if (!name)
    {
        std::snprintf(fallback, sizeof(fallback), "Slot %u",
                      static_cast<unsigned>(spec.channel_index));
        name = fallback;
    }
    return name;
}

static std::string format_broadcast_target_status(const BroadcastTargetSpec& spec)
{
    if (spec.protocol == chat::MeshProtocol::Meshtastic)
    {
        if (!spec.enabled)
        {
            return ::ui::i18n::tr("Disabled");
        }
        if (spec.channel_index == 0)
        {
            return ::ui::i18n::tr("Primary");
        }
        if (spec.channel_index == 1)
        {
            return chat::meshtastic::channelName(app::appFacade().readConfig().meshtastic_config,
                                                 spec.channel);
        }
        return spec.chat_supported ? ::ui::i18n::tr("Ready") : ::ui::i18n::tr("Slot");
    }
    if (chat::infra::isReticulumMeshProtocol(spec.protocol))
    {
        return ::ui::i18n::tr("Group destination");
    }
    const chat::MeshConfig& cfg = app::appFacade().readConfig().meshcore_config;
    const chat::MeshCoreChannelConfig& channel =
        cfg.meshCoreChannel(spec.channel_index);
    if (!spec.enabled)
    {
        return ::ui::i18n::tr("Disabled");
    }
    if (spec.channel_index == 0)
    {
        return ::ui::i18n::tr("Public");
    }
    if (chat::isAllZeroKeyBytes(channel.key, chat::kMeshCoreChannelKeyLen))
    {
        return ::ui::i18n::tr("Key missing");
    }
    return ::ui::i18n::tr("Ready");
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

static contacts::ui::ContactsTeamSnapshotSource& contacts_team_snapshot_source()
{
    if (!s_team_snapshot_source)
    {
        s_team_snapshot_source =
            std::unique_ptr<contacts::ui::ContactsTeamSnapshotSource>(
                new contacts::ui::ContactsTeamSnapshotSource(
                    team::ui::team_ui_snapshot_store()));
    }
    return *s_team_snapshot_source;
}

static bool load_contacts_team_snapshot(
    contacts::ui::ContactsTeamSnapshot& snapshot)
{
    // Team chat should be reachable once we know a team_id (e.g. after receiving TEAM_CHAT).
    return contacts_team_snapshot_source().load(snapshot) && snapshot.available;
}

static bool is_team_available()
{
    return contacts_team_snapshot_source().isAvailable();
}

static std::string contacts_team_title()
{
    contacts::ui::ContactsTeamSnapshot snapshot;
    (void)load_contacts_team_snapshot(snapshot);
    return contacts::ui::contactsTeamDisplayName(snapshot, "Team");
}

static void reset_contacts_team_action_seam()
{
    s_team_action_sink.reset();
    s_team_location_source.reset();
    s_team_chat_command_port.reset();
    s_team_action_controller = nullptr;
    s_team_snapshot_source.reset();
}

static void reset_contacts_team_chat_runtime()
{
    reset_contacts_team_action_seam();
    s_team_chat_source.reset();
}

static void delete_static_group(lv_group_t*& group)
{
    if (!group)
    {
        return;
    }
    if (lv_group_get_default() == group)
    {
        set_default_group(nullptr);
    }
    lv_group_remove_all_objs(group);
    lv_group_del(group);
    group = nullptr;
}

static void reset_conversation_runtime_state()
{
    delete_static_group(s_conv_group);
    s_conv_prev_group = nullptr;
}

static ::ui::team_actions::ITeamActionSink* contacts_team_action_sink()
{
    team::TeamController* controller = app::teamFacade().getTeamController();
    if (controller == nullptr)
    {
        reset_contacts_team_action_seam();
        return nullptr;
    }
    if (controller != s_team_action_controller)
    {
        reset_contacts_team_action_seam();
        s_team_action_controller = controller;
    }

    if (!s_team_chat_command_port)
    {
        s_team_chat_command_port =
            std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort>(
                new ::ui::team_actions::TeamControllerChatCommandPort(
                    *controller));
    }
    if (!s_team_location_source)
    {
        s_team_location_source =
            std::unique_ptr<::ui::team_actions::ITeamLocationSource>(
                new ::ui::team_actions::GpsTeamLocationSource());
    }
    if (!s_team_action_sink)
    {
        s_team_action_sink =
            std::unique_ptr<::ui::team_actions::TeamActionRuntimeSink>(
                new ::ui::team_actions::TeamActionRuntimeSink(
                    team::ui::team_ui_snapshot_store(),
                    team::ui::team_ui_chat_log_store(),
                    s_team_chat_command_port.get(),
                    s_team_location_source.get()));
    }
    return s_team_action_sink.get();
}

static ::ui::presentation_sources::TeamChatPresentationSource& contacts_team_chat_source()
{
    if (!s_team_chat_source)
    {
        s_team_chat_source =
            std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource>(
                new ::ui::presentation_sources::TeamChatPresentationSource(
                    team::ui::team_ui_snapshot_store(),
                    team::ui::team_ui_chat_log_store()));
    }
    return *s_team_chat_source;
}

static const char* team_action_failure_message(
    ::ui::UiActionResult result,
    const char* default_message,
    bool location_action = false)
{
    if (result.failure == ::ui::UiActionFailure::NotReady)
    {
        return location_action ? "Team location not ready" : "Team keys not ready";
    }
    if (result.failure == ::ui::UiActionFailure::Unsupported)
    {
        return "Team chat unsupported";
    }
    if (result.failure == ::ui::UiActionFailure::InvalidInput)
    {
        return "Message unavailable";
    }
    return default_message;
}

static uint32_t current_timestamp_seconds()
{
    uint32_t ts = sys::epoch_seconds_now();
    if (ts < 1577836800U)
    {
        ts = sys::millis_now() / 1000U;
    }
    return ts;
}

static bool is_searchable_contacts_mode(ContactsMode mode)
{
    return mode == ContactsMode::Contacts ||
           mode == ContactsMode::Nearby ||
           mode == ContactsMode::Groups ||
           mode == ContactsMode::Ignored;
}

static bool search_active()
{
    return g_contacts_state.search_query[0] != '\0';
}

static void clear_search_query()
{
    g_contacts_state.search_query[0] = '\0';
}

static constexpr const char* kLxmfAddressAcceptedChars =
    "0123456789abcdefABCDEFlxmfLXMF@:-_ \t\r\n";

static const char* trim_left(const char* text)
{
    if (!text)
    {
        return "";
    }
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
    {
        ++text;
    }
    return text;
}

static const char* skip_lxmf_prefix(const char* text)
{
    const char* cursor = trim_left(text);
    if ((cursor[0] == 'l' || cursor[0] == 'L') &&
        (cursor[1] == 'x' || cursor[1] == 'X') &&
        (cursor[2] == 'm' || cursor[2] == 'M') &&
        (cursor[3] == 'f' || cursor[3] == 'F') &&
        cursor[4] == '@')
    {
        return cursor + 5;
    }
    return cursor;
}

static void make_lxmf_contact_nickname(const chat::ReticulumPeerIdentity& identity,
                                       char* out,
                                       size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!chat::hasReticulumDestinationIdentity(identity) || out_len < 12)
    {
        std::snprintf(out, out_len, "RT-UNKNOWN");
        return;
    }
    std::snprintf(out,
                  out_len,
                  "RT-%02X%02X%02X%02X",
                  static_cast<unsigned>(identity.destination_hash[0]),
                  static_cast<unsigned>(identity.destination_hash[1]),
                  static_cast<unsigned>(identity.destination_hash[2]),
                  static_cast<unsigned>(identity.destination_hash[3]));
}

static void make_lxmf_short_name(const chat::ReticulumPeerIdentity& identity,
                                 uint32_t node_id,
                                 char* out,
                                 size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (chat::hasReticulumDestinationIdentity(identity) && out_len >= 9)
    {
        std::snprintf(out,
                      out_len,
                      "%02X%02X%02X%02X",
                      static_cast<unsigned>(identity.destination_hash[0]),
                      static_cast<unsigned>(identity.destination_hash[1]),
                      static_cast<unsigned>(identity.destination_hash[2]),
                      static_cast<unsigned>(identity.destination_hash[3]));
        return;
    }
    std::snprintf(out, out_len, "%04X", static_cast<unsigned>(node_id & 0xFFFF));
}

static bool existing_contact_matches(uint32_t node_id)
{
    return g_contacts_state.card_peer_valid && g_contacts_state.card_peer.node_id == node_id &&
           g_contacts_state.card_peer.is_contact;
}

static bool contains_ci(const char* text, const char* query)
{
    if (!query || query[0] == '\0')
    {
        return true;
    }
    if (!text)
    {
        return false;
    }
    const std::size_t query_len = std::strlen(query);
    const std::size_t text_len = std::strlen(text);
    if (query_len == 0)
    {
        return true;
    }
    if (query_len > text_len)
    {
        return false;
    }
    for (std::size_t start = 0; start + query_len <= text_len; ++start)
    {
        bool match = true;
        for (std::size_t offset = 0; offset < query_len; ++offset)
        {
            const auto lhs = static_cast<unsigned char>(text[start + offset]);
            const auto rhs = static_cast<unsigned char>(query[offset]);
            if (std::tolower(lhs) != std::tolower(rhs))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}

static bool node_matches_search(const chat::contacts::PeerDirectoryItem& node)
{
    if (!search_active())
    {
        return true;
    }

    const char* query = g_contacts_state.search_query;
    const std::string display_name = node_display_name_for_contacts(node);
    char destination_hash[chat::kReticulumPeerHashSize * 2U + 1U] = {};
    char identity_hash[chat::kReticulumPeerHashSize * 2U + 1U] = {};
    if (chat::hasReticulumDestinationIdentity(node.reticulum_identity))
    {
        format_reticulum_hash_text(node.reticulum_identity.destination_hash,
                                   destination_hash,
                                   sizeof(destination_hash));
        format_reticulum_hash_text(node.reticulum_identity.identity_hash,
                                   identity_hash,
                                   sizeof(identity_hash));
    }
    return contains_ci(node.long_name, query) ||
           contains_ci(node.display_name.c_str(), query) ||
           contains_ci(display_name.c_str(), query) ||
           contains_ci(node.short_name, query) ||
           contains_ci(destination_hash, query) ||
           contains_ci(identity_hash, query);
}

static void build_display_list(const std::vector<chat::contacts::PeerDirectoryItem>& source)
{
    g_contacts_state.display_list.clear();
    g_contacts_state.display_list.reserve(source.size());
    for (const auto& node : source)
    {
        if (node_matches_search(node))
        {
            g_contacts_state.display_list.push_back(node);
        }
    }
}

static bool use_search_display_list_for_mode(ContactsMode mode)
{
    return search_active() && is_searchable_contacts_mode(mode);
}

static const std::vector<chat::contacts::PeerDirectoryItem>* raw_list_for_mode(ContactsMode mode)
{
    return mode == ContactsMode::Groups ? &g_contacts_state.reticulum_group_list : nullptr;
}

static const std::vector<chat::contacts::PeerDirectoryItem>* selectable_list_for_mode(
    ContactsMode mode)
{
    if (use_search_display_list_for_mode(mode))
    {
        return &g_contacts_state.display_list;
    }
    return raw_list_for_mode(mode);
}

static const char* reticulum_address_save_failure_message(
    chat::MeshOperationFailure failure)
{
    switch (failure)
    {
    case chat::MeshOperationFailure::PeerKeyMissing:
        return "Address pending";
    case chat::MeshOperationFailure::NotReady:
        return "SD unavailable";
    case chat::MeshOperationFailure::InvalidInput:
        return "Invalid peer";
    case chat::MeshOperationFailure::Unsupported:
        return "Address save unsupported";
    default:
        break;
    }
    return "Address save failed";
}

static chat::MeshActionResult persist_reticulum_contact_peer(
    const chat::ReticulumPeerIdentity& identity,
    bool favorite)
{
    if (!g_contacts_state.chat_service ||
        !chat::hasReticulumDestinationIdentity(identity))
    {
        return chat::MeshActionResult::fail(chat::MeshOperationFailure::InvalidInput);
    }
    return g_contacts_state.chat_service->persistReticulumPeer(identity, favorite);
}

class ScopedReticulumContactSaveOverlay
{
  public:
    explicit ScopedReticulumContactSaveOverlay(bool active,
                                               const char* detail = nullptr)
        : active_(active)
    {
        if (!active_)
        {
            return;
        }
        ::ui::widgets::busy_overlay::show("Saving contact...", detail);
    }

    ~ScopedReticulumContactSaveOverlay()
    {
        if (!active_)
        {
            return;
        }
        ::ui::widgets::busy_overlay::hide();
    }

    ScopedReticulumContactSaveOverlay(const ScopedReticulumContactSaveOverlay&) =
        delete;
    ScopedReticulumContactSaveOverlay& operator=(
        const ScopedReticulumContactSaveOverlay&) = delete;

  private:
    bool active_ = false;
};

static bool is_search_shortcut_key(uint32_t key)
{
    return key == '/' || key == 's' || key == 'S';
}

static bool is_add_lxmf_shortcut_key(uint32_t key)
{
    return key == 'a' || key == 'A';
}

static bool is_filter_toggle_shortcut_key(uint32_t key)
{
    return key == 'f' || key == 'F';
}

static bool is_help_shortcut_key(uint32_t key)
{
    return key == 'h' || key == 'H';
}

static void bind_page_shortcuts(lv_obj_t* obj)
{
    if (obj)
    {
        lv_obj_add_event_cb(obj, contacts_handle_page_shortcut, LV_EVENT_KEY, nullptr);
    }
}

static void apply_filter_panel_visibility()
{
    if (!g_contacts_state.filter_panel)
    {
        return;
    }
    if (lv_obj_has_flag(g_contacts_state.filter_panel, LV_OBJ_FLAG_IGNORE_LAYOUT))
    {
        contacts_input_on_ui_refreshed();
        return;
    }
    if (g_contacts_state.filter_panel_visible)
    {
        lv_obj_clear_flag(g_contacts_state.filter_panel, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(g_contacts_state.filter_panel, LV_OBJ_FLAG_HIDDEN);
    }
    contacts_input_on_ui_refreshed();
}

static void toggle_filter_panel_visibility()
{
    if (g_contacts_state.filter_panel && lv_obj_has_flag(g_contacts_state.filter_panel, LV_OBJ_FLAG_HIDDEN))
        contacts_focus_to_filter();
    else contacts_focus_to_list();
}

// ---------------- Panel creation (public API) ----------------

void create_filter_panel(lv_obj_t* parent)
{
    // Structure + styles handled in layout/styles
    contacts::ui::layout::create_filter_panel(parent);
    g_contacts_state.filter_panel_visible = true;

    // Bind focus-only events here. Actual filter activation is routed through
    // two_pane_nav so focus movement stays cheap on low-power targets.
    if (g_contacts_state.contacts_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.contacts_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(g_contacts_state.contacts_btn);
    }
    if (g_contacts_state.nearby_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.nearby_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(g_contacts_state.nearby_btn);
    }
    if (g_contacts_state.groups_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.groups_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(g_contacts_state.groups_btn);
    }
    if (g_contacts_state.ignored_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.ignored_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(g_contacts_state.ignored_btn);
    }
    if (g_contacts_state.broadcast_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.broadcast_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(g_contacts_state.broadcast_btn);
    }
    if (g_contacts_state.team_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.team_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(g_contacts_state.team_btn);
    }
    if (g_contacts_state.discover_btn)
    {
        lv_obj_add_event_cb(g_contacts_state.discover_btn, on_filter_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(g_contacts_state.discover_btn);
    }
    bind_page_shortcuts(g_contacts_state.root);
    bind_page_shortcuts(g_contacts_state.top_bar.back_btn);

    // Keep highlight consistent with mode using CHECKED state
    // (visual-only; does not change behavior)
    refresh_filter_checked_state();
    apply_filter_panel_visibility();
}

// ---------------- Filter handlers (unchanged behavior) ----------------

static void on_filter_focused(lv_event_t* e)
{
    lv_obj_t* tgt = (lv_obj_t*)lv_event_get_target(e);
    ContactsMode new_mode = g_contacts_state.current_mode;
    if (!filter_mode_for_button(tgt, &new_mode))
    {
        return;
    }

    g_contacts_state.focused_filter_mode = new_mode;
    g_contacts_state.focused_filter_mode_valid = true;
    refresh_filter_checked_state();
}

bool activate_contacts_filter(lv_obj_t* filter_button)
{
    ContactsMode new_mode = g_contacts_state.current_mode;
    if (!filter_mode_for_button(filter_button, &new_mode))
    {
        return false;
    }

    g_contacts_state.focused_filter_mode = new_mode;
    g_contacts_state.focused_filter_mode_valid = true;
    if (new_mode != g_contacts_state.current_mode)
    {
        if (new_mode == ContactsMode::Discover)
        {
            g_contacts_state.last_action_mode = g_contacts_state.current_mode;
        }
        g_contacts_state.current_mode = new_mode;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
        clear_search_query();
        refresh_ui();
        if (g_contacts_state.refresh_timer)
        {
            lv_timer_reset(g_contacts_state.refresh_timer);
        }
    }
    return true;
}

static void on_list_item_clicked(lv_event_t* e)
{
    lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
    g_contacts_state.selected_index = (int)(intptr_t)lv_obj_get_user_data(item);
    if (g_contacts_state.selected_index == static_cast<int>(kBackListItemUserData))
    {
        g_contacts_state.selected_index = -1;
        contacts_focus_to_filter();
        return;
    }
    if (g_contacts_state.current_mode == ContactsMode::Groups &&
        g_contacts_state.selected_index == static_cast<int>(kAddReticulumGroupUserData))
    {
        open_reticulum_group_config_modal();
        return;
    }
    if (g_contacts_state.current_mode == ContactsMode::Discover)
    {
        execute_discovery_command(static_cast<uint8_t>(g_contacts_state.selected_index));
        return;
    }
    if (g_contacts_state.current_mode == ContactsMode::Broadcast)
    {
        BroadcastTargetSpec spec{};
        if (get_selected_broadcast_target(&spec, nullptr) && !spec.chat_supported)
        {
            ::ui::feedback::show_notice(broadcast_chat_unavailable_message(spec), 2200);
            return;
        }
    }
    if (g_contacts_state.current_mode == ContactsMode::Groups &&
        !chat_support::supports_reticulum_destination_text())
    {
        ::ui::feedback::show_notice(
            chat_support::reticulum_destination_text_unavailable_message(),
            2200);
        return;
    }
    open_action_menu_modal();
}

static void on_list_item_focused(lv_event_t* e)
{
    if (!e)
    {
        return;
    }
    lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
    if (!item || !lv_obj_is_valid(item))
    {
        return;
    }
    const intptr_t user_data = reinterpret_cast<intptr_t>(lv_obj_get_user_data(item));
    if (user_data >= 0)
    {
        g_contacts_state.selected_index = static_cast<int>(user_data);
    }
    lv_obj_scroll_to_view(item, LV_ANIM_OFF);
}

static void on_prev_clicked(lv_event_t* /*e*/)
{
    if (lv_obj_has_state(g_contacts_state.prev_btn, LV_STATE_DISABLED)) return;
    g_contacts_state.current_page--;
    if (g_contacts_state.current_page < 0)
    {
        // Wrap-around: jump to the last page.
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
        // Wrap-around: go back to the first page.
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

static const chat::contacts::PeerDirectoryItem* get_selected_node()
{
    return uses_node_card() && g_contacts_state.card_peer_valid ? &g_contacts_state.card_peer : nullptr;
}

static const chat::contacts::PeerDirectoryItem* get_selected_reticulum_group()
{
    if (g_contacts_state.current_mode != ContactsMode::Groups ||
        g_contacts_state.selected_index < 0)
    {
        return nullptr;
    }
    const auto* list = selectable_list_for_mode(g_contacts_state.current_mode);
    if (!list ||
        g_contacts_state.selected_index >= static_cast<int>(list->size()))
    {
        return nullptr;
    }
    return &(*list)[g_contacts_state.selected_index];
}

static const chat::contacts::PeerDirectoryItem* find_node_by_id(uint32_t node_id)
{
    return g_contacts_state.card_peer_valid && g_contacts_state.card_peer.node_id == node_id ? &g_contacts_state.card_peer : nullptr;
}

static bool reticulum_identity_hash_present(
    const chat::ReticulumPeerIdentity& identity)
{
    if (!chat::hasReticulumDestinationIdentity(identity))
    {
        return false;
    }
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        if (identity.identity_hash[index] != 0)
        {
            return true;
        }
    }
    return false;
}

static void merge_reticulum_projection_for_details(
    chat::contacts::PeerDirectoryItem& target,
    const chat::contacts::PeerDirectoryItem& projection)
{
    if (!is_reticulum_node(projection))
    {
        return;
    }

    if (target.protocol != chat::contacts::NodeProtocolType::Reticulum)
    {
        target.protocol = chat::contacts::NodeProtocolType::Reticulum;
    }
    if (!reticulum_identity_hash_present(target.reticulum_identity) &&
        chat::hasReticulumDestinationIdentity(projection.reticulum_identity))
    {
        target.reticulum_identity = projection.reticulum_identity;
    }
    if (target.last_seen == 0 && projection.last_seen != 0)
    {
        target.last_seen = projection.last_seen;
    }
    if (std::isnan(target.snr) && !std::isnan(projection.snr))
    {
        target.snr = projection.snr;
    }
    if (std::isnan(target.rssi) && !std::isnan(projection.rssi))
    {
        target.rssi = projection.rssi;
    }
    if (target.hops_away == 0xFF && projection.hops_away != 0xFF)
    {
        target.hops_away = projection.hops_away;
    }
    if (target.long_name[0] == '\0' && projection.long_name[0] != '\0')
    {
        lv_strlcpy(target.long_name, projection.long_name, sizeof(target.long_name));
    }
    if (target.display_name.empty() && !projection.display_name.empty())
    {
        target.display_name = projection.display_name;
    }
}

static bool get_selected_broadcast_target(BroadcastTargetSpec* out_spec,
                                          std::string* out_title)
{
    if (g_contacts_state.current_mode != ContactsMode::Broadcast || !out_spec)
    {
        return false;
    }
    BroadcastTargetSpec spec{};
    if (!get_broadcast_target_spec(g_contacts_state.selected_index, &spec))
    {
        return false;
    }
    *out_spec = spec;
    if (out_title)
    {
        *out_title = format_broadcast_target_label(spec);
    }
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
    return ::ui::components::floating_search_box::is_open(g_contacts_state.search_box) ||
           ::ui::components::floating_search_box::is_open(g_contacts_state.lxmf_address_box) ||
           g_contacts_state.add_edit_modal != nullptr ||
           g_contacts_state.reticulum_group_modal != nullptr ||
           g_contacts_state.del_confirm_modal != nullptr ||
           g_contacts_state.action_menu_modal != nullptr ||
           g_contacts_state.discover_modal != nullptr ||
           ::ui::components::shortcut_help_modal::is_open(s_help_modal);
}

static void on_search_apply(const char* text, void* /*user_data*/)
{
    std::snprintf(g_contacts_state.search_query,
                  sizeof(g_contacts_state.search_query),
                  "%s",
                  text ? text : "");
    g_contacts_state.current_page = 0;
    g_contacts_state.selected_index = -1;
    refresh_ui();
    contacts_focus_to_list();
}

static void on_search_clear(void* /*user_data*/)
{
    g_contacts_state.search_query[0] = '\0';
    g_contacts_state.current_page = 0;
    g_contacts_state.selected_index = -1;
    refresh_ui();
    contacts_focus_to_list();
}

static void on_search_cancel(void* /*user_data*/)
{
    contacts_focus_to_list();
}

static void open_search_modal()
{
    if (::ui::components::floating_search_box::is_open(g_contacts_state.search_box))
    {
        ::ui::components::floating_search_box::focus(g_contacts_state.search_box);
        return;
    }
    if (!is_searchable_contacts_mode(g_contacts_state.current_mode))
    {
        ::ui::feedback::show_notice("Search current contacts list", 1600);
        return;
    }
    if (is_any_modal_open())
    {
        return;
    }

    ::ui::components::floating_search_box::Config config{};
    config.title = "Search contacts";
    config.initial_text = g_contacts_state.search_query;
    config.max_length = sizeof(g_contacts_state.search_query) - 1U;
    config.restore_group = contacts_input_get_group();
    config.callbacks.apply = on_search_apply;
    config.callbacks.clear = on_search_clear;
    config.callbacks.cancel = on_search_cancel;
    (void)::ui::components::floating_search_box::open(
        g_contacts_state.search_box,
        g_contacts_state.root ? g_contacts_state.root : lv_screen_active(),
        config);
}

static void on_lxmf_address_apply(const char* text, void* /*user_data*/)
{
    if (!g_contacts_state.contact_service)
    {
        ::ui::feedback::show_notice("Contacts unavailable", 1800);
        contacts_focus_to_list();
        return;
    }

    chat::ReticulumPeerIdentity identity{};
    char error[64] = {};
    const char* address_text = skip_lxmf_prefix(text);
    if (!chat::parseReticulumDestinationHashText(address_text,
                                                 &identity,
                                                 error,
                                                 sizeof(error)))
    {
        ::ui::feedback::show_notice(error[0] != '\0' ? error : "Invalid LXMF address",
                                    2200);
        contacts_focus_to_list();
        return;
    }

    rtdir::LxmfAddressRecord address_record{};
    const auto address_lookup =
        rtdir::find_lxmf_address_by_destination(identity.destination_hash,
                                                &address_record);
    const bool has_full_address =
        address_lookup.loaded && address_record.valid;
    if (has_full_address)
    {
        identity = chat::makeReticulumPeerIdentity(address_record.destination_hash,
                                                   address_record.identity_hash);
    }

    uint32_t node_id = 0;
    if (!g_contacts_state.contact_service->findNodeIdByReticulumDestinationHash(
            identity.destination_hash,
            &node_id))
    {
        node_id = chat::reticulum::nodeIdFromDestinationHash(identity.destination_hash);
    }

    const chat::contacts::PeerDirectoryItem* existing_node = find_node_by_id(node_id);
    char short_name[10] = {};
    char nickname[13] = {};
    char generated_nickname[13] = {};
    make_lxmf_short_name(identity, node_id, short_name, sizeof(short_name));
    make_lxmf_contact_nickname(identity, generated_nickname, sizeof(generated_nickname));
    lv_strlcpy(nickname, generated_nickname, sizeof(nickname));
    const char* address_display_name =
        has_full_address && address_record.display_name[0] != '\0'
            ? address_record.display_name
            : nullptr;
    if (address_display_name && std::strlen(address_display_name) <= 12U)
    {
        lv_strlcpy(nickname, address_display_name, sizeof(nickname));
    }
    else if (existing_node)
    {
        const std::string existing_name = node_display_name_for_contacts(*existing_node);
        if (!existing_name.empty() && existing_name.size() <= 12U)
        {
            lv_strlcpy(nickname, existing_name.c_str(), sizeof(nickname));
        }
    }

    chat::contacts::NodeUpdate update{};
    update.short_name = short_name;
    char fallback_long_name[32] = {};
    if (address_display_name)
    {
        std::snprintf(fallback_long_name,
                      sizeof(fallback_long_name),
                      "%s",
                      address_display_name);
        update.long_name = fallback_long_name;
    }
    else if (!existing_node || existing_node->long_name[0] == '\0')
    {
        std::snprintf(fallback_long_name, sizeof(fallback_long_name), "%s", nickname);
        update.long_name = fallback_long_name;
    }
    update.has_last_seen = true;
    update.last_seen = current_timestamp_seconds();
    update.has_protocol = true;
    update.protocol = static_cast<uint8_t>(chat::contacts::NodeProtocolType::Reticulum);
    update.has_role = true;
    update.role = static_cast<uint8_t>(chat::contacts::NodeRoleType::Client);
    update.reticulum_identity = identity;

    ScopedReticulumContactSaveOverlay save_overlay(
        true,
        has_full_address ? "Updating SD address book" : "Saving local contact");
    g_contacts_state.contact_service->applyNodeUpdate(node_id, update);

    refresh_contacts_data();
    const bool was_contact = existing_contact_matches(node_id);
    bool added = was_contact || g_contacts_state.contact_service->addContact(node_id, nickname);
    if (!added && std::strcmp(nickname, generated_nickname) != 0)
    {
        added = g_contacts_state.contact_service->addContact(node_id, generated_nickname);
    }
    if (!added)
    {
        ::ui::feedback::show_notice("Contact save failed", 2200);
        contacts_focus_to_list();
        return;
    }

    bool favorite_saved = false;
    if (has_full_address)
    {
        const auto favorite_status =
            rtdir::set_lxmf_address_favorite_now(identity.destination_hash, true);
        favorite_saved = favorite_status.saved;
        if (!favorite_saved)
        {
            std::printf("[Contacts][RT] favorite_save failed message=%s detail=%s\n",
                        favorite_status.message,
                        favorite_status.detail);
        }
    }

    clear_search_query();
    g_contacts_state.current_mode = ContactsMode::Contacts;
    g_contacts_state.current_page = 0;
    g_contacts_state.selected_index = -1;
    refresh_contacts_data();
    refresh_ui();
    ::ui::feedback::show_notice(
        has_full_address
            ? (favorite_saved ? "Contact saved" : "Contact added; SD pending")
            : "Contact added; wait announce",
        1800);
    contacts_focus_to_list();
}

static void on_lxmf_address_clear(void* /*user_data*/)
{
    contacts_focus_to_list();
}

static void on_lxmf_address_cancel(void* /*user_data*/)
{
    contacts_focus_to_list();
}

static void open_lxmf_address_modal()
{
    if (::ui::components::floating_search_box::is_open(g_contacts_state.lxmf_address_box))
    {
        ::ui::components::floating_search_box::focus(g_contacts_state.lxmf_address_box);
        return;
    }
    if (!uses_reticulum_filter_profile())
    {
        ::ui::feedback::show_notice("LXMF address is Reticulum only", 1800);
        return;
    }
    if (is_any_modal_open())
    {
        return;
    }

    ::ui::components::floating_search_box::Config config{};
    config.title = "Add LXMF Address";
    config.initial_text = "";
    config.accepted_chars = kLxmfAddressAcceptedChars;
    config.max_length = chat::kReticulumPeerHashSize * 2U + 5U;
    config.restore_group = contacts_input_get_group();
    config.callbacks.apply = on_lxmf_address_apply;
    config.callbacks.clear = on_lxmf_address_clear;
    config.callbacks.cancel = on_lxmf_address_cancel;
    (void)::ui::components::floating_search_box::open(
        g_contacts_state.lxmf_address_box,
        g_contacts_state.root ? g_contacts_state.root : lv_screen_active(),
        config);
}

static void contacts_handle_page_shortcut(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    if (is_search_shortcut_key(key))
    {
        open_search_modal();
        lv_event_stop_processing(event);
        return;
    }
    if (is_add_lxmf_shortcut_key(key))
    {
        open_lxmf_address_modal();
        lv_event_stop_processing(event);
        return;
    }
    if (is_filter_toggle_shortcut_key(key))
    {
        toggle_filter_panel_visibility();
        lv_event_stop_processing(event);
        return;
    }
    if (is_help_shortcut_key(key))
    {
        open_contacts_help_modal();
        lv_event_stop_processing(event);
        return;
    }
}

static void close_contacts_help_modal()
{
    ::ui::components::shortcut_help_modal::close(s_help_modal);
}

static void open_contacts_help_modal()
{
    if (::ui::components::shortcut_help_modal::is_open(s_help_modal))
    {
        close_contacts_help_modal();
        return;
    }
    if (is_any_modal_open())
    {
        return;
    }

    ::ui::components::shortcut_help_modal::Row rows[7] = {};
    std::size_t row_count = 0;
    rows[row_count++] = {"S", "/", "Search names"};
    if (uses_reticulum_filter_profile())
    {
        rows[row_count++] = {"A", nullptr, "Add LXMF address"};
    }
    rows[row_count++] = {"F", nullptr, "Show or hide filters"};
    rows[row_count++] = {"Enter", nullptr, "Open selected item"};
    rows[row_count++] = {"Back", nullptr, "Return or close"};
    rows[row_count++] = {"H", nullptr, "Close help"};
    if (uses_reticulum_filter_profile())
    {
        rows[row_count++] = {"Groups", nullptr, "Add opens group config"};
    }

    ::ui::components::shortcut_help_modal::Config config{};
    config.title = "Contacts Help";
    config.rows = rows;
    config.row_count = row_count;
    config.width = 304;
    config.height = 176;
    config.restore_group = contacts_input_get_group();
    (void)::ui::components::shortcut_help_modal::open(
        s_help_modal,
        g_contacts_state.root ? g_contacts_state.root : lv_screen_active(),
        config);
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
    apply_primary_text(title);
    ::ui::i18n::set_label_text(title, is_edit ? "Edit nickname" : "Enter nickname");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    g_contacts_state.add_edit_textarea = lv_textarea_create(win);
    ::ui::widgets::attach_touch_text_editor(g_contacts_state.add_edit_textarea);
    lv_textarea_set_one_line(g_contacts_state.add_edit_textarea, true);
    lv_textarea_set_max_length(g_contacts_state.add_edit_textarea, 12);
    lv_obj_set_width(g_contacts_state.add_edit_textarea, LV_PCT(100));
    lv_obj_align(g_contacts_state.add_edit_textarea, LV_ALIGN_TOP_MID, 0, ::ui::page_profile::current().large_touch_hitbox ? 40 : 26);

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
    lv_obj_set_size(save_btn, ::ui::page_profile::resolve_control_button_min_width(), ::ui::page_profile::resolve_control_button_height());
    contacts::ui::style::apply_btn_basic(save_btn);
    lv_obj_t* save_label = lv_label_create(save_btn);
    apply_primary_text(save_label);
    ::ui::i18n::set_label_text(save_label, "Save");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, on_add_edit_save_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, ::ui::page_profile::resolve_control_button_min_width(), ::ui::page_profile::resolve_control_button_height());
    contacts::ui::style::apply_btn_basic(cancel_btn);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    apply_primary_text(cancel_label);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_add_edit_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g_contacts_state.modal_group, g_contacts_state.add_edit_textarea);
    lv_group_add_obj(g_contacts_state.modal_group, save_btn);
    lv_group_add_obj(g_contacts_state.modal_group, cancel_btn);
    lv_group_focus_obj(g_contacts_state.add_edit_textarea);
}

static void show_reticulum_group_error(const char* message)
{
    if (!g_contacts_state.reticulum_group_error_label)
    {
        return;
    }
    ::ui::i18n::set_label_text(g_contacts_state.reticulum_group_error_label,
                               message ? message : "Save failed");
    lv_obj_clear_flag(g_contacts_state.reticulum_group_error_label, LV_OBJ_FLAG_HIDDEN);
}

static bool reticulum_group_storage_ready_for_edit()
{
    const chat::MeshProtocol protocol = chat::infra::normalizeMeshProtocol(
        app::configFacade().readConfig().mesh_protocol);
    const bool available = chat::infra::isReticulumMeshProtocol(protocol);
    g_contacts_state.reticulum_group_storage_supported = available;
    g_contacts_state.reticulum_group_storage_ready = available;
    g_contacts_state.reticulum_group_storage_loaded = available;
    std::snprintf(g_contacts_state.reticulum_group_storage_message,
                  sizeof(g_contacts_state.reticulum_group_storage_message),
                  "%s",
                  available ? "" : "Reticulum required");
    std::snprintf(g_contacts_state.reticulum_group_storage_detail,
                  sizeof(g_contacts_state.reticulum_group_storage_detail),
                  "%s",
                  available ? "" : "Stored in working configuration");
    return available;
}

static void open_reticulum_group_config_modal()
{
    if (g_contacts_state.reticulum_group_modal)
    {
        return;
    }
    if (!reticulum_group_storage_ready_for_edit())
    {
        const char* message = g_contacts_state.reticulum_group_storage_message[0] != '\0'
                                  ? g_contacts_state.reticulum_group_storage_message
                                  : "Reticulum required";
        ::ui::feedback::show_notice(message, 2200);
        contacts_focus_to_list();
        return;
    }

    modal_prepare_group();
    g_contacts_state.reticulum_group_modal = create_modal_root(280, 210);
    lv_obj_t* win = lv_obj_get_child(g_contacts_state.reticulum_group_modal, 0);
    if (!win)
    {
        modal_close(g_contacts_state.reticulum_group_modal);
        return;
    }

    lv_obj_t* title = lv_label_create(win);
    apply_primary_text(title);
    ::ui::i18n::set_label_text(title, "Add Reticulum Group");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* name_label = lv_label_create(win);
    apply_primary_text(name_label);
    ::ui::i18n::set_label_text(name_label, "Name");
    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 0, 28);

    g_contacts_state.reticulum_group_name_textarea = lv_textarea_create(win);
    ::ui::widgets::attach_touch_text_editor(g_contacts_state.reticulum_group_name_textarea);
    lv_textarea_set_one_line(g_contacts_state.reticulum_group_name_textarea, true);
    lv_textarea_set_max_length(g_contacts_state.reticulum_group_name_textarea,
                               chat::kReticulumGroupNameMaxLen - 1);
    lv_obj_set_width(g_contacts_state.reticulum_group_name_textarea, LV_PCT(100));
    lv_obj_align(g_contacts_state.reticulum_group_name_textarea, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t* destination_label = lv_label_create(win);
    apply_primary_text(destination_label);
    ::ui::i18n::set_label_text(destination_label, "Destination hash");
    lv_obj_align(destination_label, LV_ALIGN_TOP_LEFT, 0, 78);

    g_contacts_state.reticulum_group_destination_textarea = lv_textarea_create(win);
    ::ui::widgets::attach_touch_text_editor(g_contacts_state.reticulum_group_destination_textarea);
    lv_textarea_set_one_line(g_contacts_state.reticulum_group_destination_textarea, true);
    lv_textarea_set_max_length(g_contacts_state.reticulum_group_destination_textarea, 48);
    lv_obj_set_width(g_contacts_state.reticulum_group_destination_textarea, LV_PCT(100));
    lv_obj_align(g_contacts_state.reticulum_group_destination_textarea, LV_ALIGN_TOP_MID, 0, 94);

    g_contacts_state.reticulum_group_error_label = lv_label_create(win);
    lv_label_set_text(g_contacts_state.reticulum_group_error_label, "");
    lv_obj_set_style_text_color(g_contacts_state.reticulum_group_error_label, lv_color_hex(kColorWarn), 0);
    lv_obj_align(g_contacts_state.reticulum_group_error_label, LV_ALIGN_TOP_MID, 0, 126);
    lv_obj_add_flag(g_contacts_state.reticulum_group_error_label, LV_OBJ_FLAG_HIDDEN);

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
    lv_obj_set_size(save_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    contacts::ui::style::apply_btn_basic(save_btn);
    lv_obj_t* save_label = lv_label_create(save_btn);
    apply_primary_text(save_label);
    ::ui::i18n::set_label_text(save_label, "Save");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save_btn, on_reticulum_group_save_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn,
                    ::ui::page_profile::resolve_control_button_min_width(),
                    ::ui::page_profile::resolve_control_button_height());
    contacts::ui::style::apply_btn_basic(cancel_btn);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    apply_primary_text(cancel_label);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_reticulum_group_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g_contacts_state.modal_group, g_contacts_state.reticulum_group_name_textarea);
    lv_group_add_obj(g_contacts_state.modal_group, g_contacts_state.reticulum_group_destination_textarea);
    lv_group_add_obj(g_contacts_state.modal_group, save_btn);
    lv_group_add_obj(g_contacts_state.modal_group, cancel_btn);
    lv_group_focus_obj(g_contacts_state.reticulum_group_name_textarea);
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

    const std::string delete_name = node_display_name_for_contacts(*node);
    const std::string msg = ::ui::i18n::format("Delete contact %s?", delete_name.c_str());
    lv_obj_t* label = lv_label_create(win);
    apply_primary_text(label);
    ::ui::i18n::set_label_text_raw(label, msg.c_str());
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
    lv_obj_set_size(confirm_btn, ::ui::page_profile::resolve_control_button_min_width(), ::ui::page_profile::resolve_control_button_height());
    contacts::ui::style::apply_btn_basic(confirm_btn);
    lv_obj_t* confirm_label = lv_label_create(confirm_btn);
    apply_primary_text(confirm_label);
    ::ui::i18n::set_label_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);
    lv_obj_add_event_cb(confirm_btn, on_del_confirm_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, ::ui::page_profile::resolve_control_button_min_width(), ::ui::page_profile::resolve_control_button_height());
    contacts::ui::style::apply_btn_basic(cancel_btn);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    apply_primary_text(cancel_label);
    ::ui::i18n::set_label_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel_btn, on_del_cancel_clicked, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g_contacts_state.modal_group, confirm_btn);
    lv_group_add_obj(g_contacts_state.modal_group, cancel_btn);
    lv_group_focus_obj(cancel_btn);
}

static bool reticulum_hash_is_zero(const uint8_t* hash)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        if (hash[index] != 0)
        {
            return false;
        }
    }
    return true;
}

static void format_reticulum_hash_or_empty(const uint8_t* hash, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!hash || reticulum_hash_is_zero(hash))
    {
        std::snprintf(out, out_len, "--");
        return;
    }
    format_reticulum_hash_text(hash, out, out_len);
}

static void add_reticulum_detail_row(lv_obj_t* parent,
                                     const char* label_text,
                                     const char* value_text)
{
    if (!parent)
    {
        return;
    }
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(row, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(row, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(kColorPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(kColorLine), LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(row);
    ::ui::i18n::set_label_text(label, label_text);
    contacts::ui::style::apply_label_muted(label);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, ::ui::page_profile::resolve_caption_font(), 0);

    lv_obj_t* value = lv_label_create(row);
    ::ui::i18n::set_content_label_text_raw(value,
                                           value_text && value_text[0] != '\0' ? value_text : "--");
    contacts::ui::style::apply_label_primary(value);
    lv_obj_set_width(value, LV_PCT(100));
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
}

static const char* reticulum_node_status_text(const chat::contacts::PeerDirectoryItem& node,
                                              char* out,
                                              size_t out_len)
{
    if (!out || out_len == 0)
    {
        return "";
    }
    const std::string seen = format_time_status(node.last_seen);
    std::snprintf(out,
                  out_len,
                  "%s%s%s",
                  node.is_contact ? "Contact" : "Nearby",
                  seen.empty() ? "" : " / ",
                  seen.empty() ? "" : seen.c_str());
    return out;
}

static const char* reticulum_link_text(const chat::contacts::PeerDirectoryItem& node,
                                       char* out,
                                       size_t out_len)
{
    if (!out || out_len == 0)
    {
        return "";
    }
    std::string text;
    if (node.hops_away != 0xFF)
    {
        text = std::to_string(static_cast<unsigned>(node.hops_away)) + " hops";
    }
    else
    {
        text = "--";
    }
    if (!std::isnan(node.snr))
    {
        text += " / SNR ";
        text += std::to_string(static_cast<int>(std::round(node.snr)));
    }
    if (!std::isnan(node.rssi))
    {
        text += " / RSSI ";
        text += std::to_string(static_cast<int>(std::round(node.rssi)));
        text += " dBm";
    }
    std::snprintf(out, out_len, "%s", text.c_str());
    return out;
}

static void reticulum_node_info_back_requested(void*)
{
    close_node_info_screen();
}

static void open_reticulum_node_info_screen(const chat::contacts::PeerDirectoryItem& node,
                                            lv_obj_t* parent)
{
    if (!parent)
    {
        return;
    }

    g_contacts_state.reticulum_node_info_active = true;
    s_reticulum_node_info_top_bar = ::ui::widgets::TopBar{};

    lv_obj_t* root = lv_obj_create(parent);
    g_contacts_state.node_info_root = root;
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root, 0, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(root, lv_color_hex(0xF6E6C6), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(root);

    lv_obj_t* header = lv_obj_create(root);
    lv_obj_set_size(header, LV_PCT(100), ::ui::page_profile::current().top_bar_height);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    ::ui::widgets::TopBarConfig cfg{};
    cfg.height = ::ui::page_profile::current().top_bar_height;
    ::ui::widgets::top_bar_init(s_reticulum_node_info_top_bar, header, cfg);
    ::ui::widgets::top_bar_set_title(s_reticulum_node_info_top_bar, ::ui::i18n::tr("Reticulum Peer"));
    ::ui::widgets::top_bar_set_back_callback(s_reticulum_node_info_top_bar,
                                             reticulum_node_info_back_requested,
                                             nullptr);
    ui_update_top_bar_battery(s_reticulum_node_info_top_bar);

    lv_obj_t* content = lv_obj_create(root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_height(content, 0);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(content, lv_color_hex(0xFAF0D8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(content, on_node_info_key, LV_EVENT_KEY, nullptr);

    const std::string display_name = node_display_name_for_contacts(node);
    lv_obj_t* title = lv_label_create(content);
    ::ui::i18n::set_content_label_text_raw(title, display_name.c_str());
    contacts::ui::style::apply_label_primary(title);
    lv_obj_set_width(title, LV_PCT(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);

    char destination_hash[chat::kReticulumPeerHashSize * 2U + 1U] = {};
    char identity_hash[chat::kReticulumPeerHashSize * 2U + 1U] = {};
    if (chat::hasReticulumDestinationIdentity(node.reticulum_identity))
    {
        format_reticulum_hash_or_empty(node.reticulum_identity.destination_hash,
                                       destination_hash,
                                       sizeof(destination_hash));
        format_reticulum_hash_or_empty(node.reticulum_identity.identity_hash,
                                       identity_hash,
                                       sizeof(identity_hash));
    }
    else
    {
        std::snprintf(destination_hash, sizeof(destination_hash), "--");
        std::snprintf(identity_hash, sizeof(identity_hash), "--");
    }

    char node_id[16] = {};
    char status[64] = {};
    char link[80] = {};
    std::snprintf(node_id, sizeof(node_id), "%08lX", static_cast<unsigned long>(node.node_id));

    add_reticulum_detail_row(content, "Display Name", display_name.c_str());
    add_reticulum_detail_row(content, "LXMF Address", destination_hash);
    add_reticulum_detail_row(content, "Identity Hash", identity_hash);
    add_reticulum_detail_row(content, "Node ID", node_id);
    add_reticulum_detail_row(content, "Status", reticulum_node_status_text(node, status, sizeof(status)));
    add_reticulum_detail_row(content, "Link", reticulum_link_text(node, link, sizeof(link)));
    add_reticulum_detail_row(content, "Protocol", "Reticulum / LXMF");

    if (!g_contacts_state.node_info_group)
    {
        g_contacts_state.node_info_group = lv_group_create();
    }
    g_contacts_state.node_info_prev_group = lv_group_get_default();
    set_default_group(g_contacts_state.node_info_group);
    if (s_reticulum_node_info_top_bar.back_btn)
    {
        lv_group_add_obj(g_contacts_state.node_info_group, s_reticulum_node_info_top_bar.back_btn);
        lv_obj_add_event_cb(s_reticulum_node_info_top_bar.back_btn,
                            on_node_info_key,
                            LV_EVENT_KEY,
                            nullptr);
        lv_group_focus_obj(s_reticulum_node_info_top_bar.back_btn);
    }
    lv_group_add_obj(g_contacts_state.node_info_group, content);

    if (g_contacts_state.root)
    {
        lv_obj_add_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_contacts_state.refresh_timer)
    {
        lv_timer_pause(g_contacts_state.refresh_timer);
    }
}

static void open_node_info_screen_for_node(uint32_t node_id)
{
    CONTACTS_NODE_INFO_LOG("open request node=%08lX existing_root=%p contacts_root=%p active=%p\n",
                           static_cast<unsigned long>(node_id),
                           g_contacts_state.node_info_root,
                           g_contacts_state.root,
                           lv_screen_active());
    if (g_contacts_state.node_info_root)
    {
        CONTACTS_NODE_INFO_LOG("open ignored: node_info_root already exists\n");
        return;
    }

    const chat::contacts::PeerDirectoryItem* node = find_node_by_id(node_id);
    if (!node && g_contacts_state.contact_service)
    {
        node = g_contacts_state.contact_service->getPeerByNodeId(node_id);
    }
    if (!node)
    {
        CONTACTS_NODE_INFO_LOG("open aborted: node not found\n");
        return;
    }

    CONTACTS_NODE_INFO_LOG("resolved node=%08lX display='%s' long='%s' short='%s' pos_valid=%d\n",
                           static_cast<unsigned long>(node->node_id),
                           node->display_name.c_str(),
                           node->long_name,
                           node->short_name,
                           node->position.valid ? 1 : 0);

    lv_obj_t* parent = g_contacts_state.root
                           ? lv_obj_get_parent(g_contacts_state.root)
                           : lv_screen_active();
    if (!parent)
    {
        CONTACTS_NODE_INFO_LOG("open aborted: parent missing\n");
        return;
    }
    lv_obj_update_layout(parent);
    CONTACTS_NODE_INFO_LOG("parent=%p size=%dx%d hidden=%d\n",
                           parent,
                           static_cast<int>(lv_obj_get_width(parent)),
                           static_cast<int>(lv_obj_get_height(parent)),
                           lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);

    chat::contacts::PeerDirectoryItem detail_info = *node;
    if (g_contacts_state.contact_service)
    {
        const auto* latest = g_contacts_state.contact_service->getPeerByNodeId(node->node_id);
        if (latest)
        {
            detail_info = *latest;
            merge_reticulum_projection_for_details(detail_info, *node);
            CONTACTS_NODE_INFO_LOG("using latest contact_service snapshot node=%08lX pos_valid=%d\n",
                                   static_cast<unsigned long>(latest->node_id),
                                   latest->position.valid ? 1 : 0);
        }
    }

    if (is_reticulum_node(detail_info))
    {
        open_reticulum_node_info_screen(detail_info, parent);
        CONTACTS_NODE_INFO_LOG("reticulum node_info opened\n");
        return;
    }

    node_info::ui::NodeInfoWidgets widgets = node_info::ui::create(parent);
    g_contacts_state.node_info_root = widgets.root;
    CONTACTS_NODE_INFO_LOG("node_info created root=%p header=%p content=%p back_btn=%p\n",
                           widgets.root,
                           widgets.header,
                           widgets.content,
                           widgets.back_btn);

    node_info::ui::set_node_info(detail_info);
    CONTACTS_NODE_INFO_LOG("set_node_info done root=%p hidden=%d size=%dx%d\n",
                           widgets.root,
                           widgets.root ? (lv_obj_has_flag(widgets.root, LV_OBJ_FLAG_HIDDEN) ? 1 : 0) : -1,
                           widgets.root ? static_cast<int>(lv_obj_get_width(widgets.root)) : -1,
                           widgets.root ? static_cast<int>(lv_obj_get_height(widgets.root)) : -1);

    if (!g_contacts_state.node_info_group)
    {
        g_contacts_state.node_info_group = lv_group_create();
        CONTACTS_NODE_INFO_LOG("created node_info_group=%p\n", g_contacts_state.node_info_group);
    }
    lv_group_remove_all_objs(g_contacts_state.node_info_group);
    g_contacts_state.node_info_prev_group = lv_group_get_default();
    set_default_group(g_contacts_state.node_info_group);
    CONTACTS_NODE_INFO_LOG("focus group switched prev=%p current=%p\n",
                           g_contacts_state.node_info_prev_group,
                           g_contacts_state.node_info_group);

    node_info::ui::InputCallbacks input_callbacks{};
    input_callbacks.back_requested = reticulum_node_info_back_requested;
    node_info::ui::bind_input_group(g_contacts_state.node_info_group,
                                    input_callbacks);
    CONTACTS_NODE_INFO_LOG("node_info input bound back_btn=%p\n", widgets.back_btn);

    if (g_contacts_state.root)
    {
        lv_obj_add_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
        CONTACTS_NODE_INFO_LOG("contacts root hidden root=%p hidden=%d\n",
                               g_contacts_state.root,
                               lv_obj_has_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    }
    if (g_contacts_state.refresh_timer)
    {
        lv_timer_pause(g_contacts_state.refresh_timer);
        CONTACTS_NODE_INFO_LOG("refresh timer paused timer=%p\n", g_contacts_state.refresh_timer);
    }
    CONTACTS_NODE_INFO_LOG("open complete\n");
}

static void close_node_info_screen()
{
    CONTACTS_NODE_INFO_LOG("close request root=%p group=%p prev_group=%p\n",
                           g_contacts_state.node_info_root,
                           g_contacts_state.node_info_group,
                           g_contacts_state.node_info_prev_group);
    if (!g_contacts_state.node_info_root)
    {
        CONTACTS_NODE_INFO_LOG("close ignored: no node_info_root\n");
        return;
    }

    if (g_contacts_state.reticulum_node_info_active)
    {
        if (g_contacts_state.node_info_root && lv_obj_is_valid(g_contacts_state.node_info_root))
        {
            lv_obj_del(g_contacts_state.node_info_root);
        }
        s_reticulum_node_info_top_bar = ::ui::widgets::TopBar{};
        g_contacts_state.reticulum_node_info_active = false;
    }
    else
    {
        node_info::ui::destroy();
    }
    g_contacts_state.node_info_root = nullptr;
    CONTACTS_NODE_INFO_LOG("node_info destroyed\n");

    if (g_contacts_state.node_info_group)
    {
        lv_group_remove_all_objs(g_contacts_state.node_info_group);
        CONTACTS_NODE_INFO_LOG("node_info_group cleared group=%p\n", g_contacts_state.node_info_group);
    }

    lv_group_t* restore = contacts_input_get_group();
    if (restore)
    {
        set_default_group(restore);
        CONTACTS_NODE_INFO_LOG("restored default group=%p\n", restore);
    }
    g_contacts_state.node_info_prev_group = nullptr;

    if (g_contacts_state.root)
    {
        lv_obj_clear_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
        CONTACTS_NODE_INFO_LOG("contacts root shown root=%p hidden=%d\n",
                               g_contacts_state.root,
                               lv_obj_has_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
    }
    if (g_contacts_state.refresh_timer)
    {
        lv_timer_resume(g_contacts_state.refresh_timer);
        CONTACTS_NODE_INFO_LOG("refresh timer resumed timer=%p\n", g_contacts_state.refresh_timer);
    }

    CONTACTS_NODE_INFO_LOG("close refresh_ui\n");
    refresh_ui();
    CONTACTS_NODE_INFO_LOG("close contacts_focus_to_list\n");
    contacts_focus_to_list();
    CONTACTS_NODE_INFO_LOG("close complete\n");
}

static void open_chat_compose()
{
    const auto* node = get_selected_node();
    const auto* group = get_selected_reticulum_group();
    if (g_contacts_state.current_mode != ContactsMode::Broadcast &&
        g_contacts_state.current_mode != ContactsMode::Team &&
        g_contacts_state.current_mode != ContactsMode::Groups &&
        !node)
    {
        return;
    }
    if (g_contacts_state.current_mode == ContactsMode::Groups && !group)
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
    if (lv_obj_t* chat_parent = chat::ui::shell::get_container())
    {
        if (lv_obj_is_valid(chat_parent))
        {
            parent = chat_parent;
        }
    }

    chat::ChannelId channel = chat::ChannelId::PRIMARY;
    uint32_t peer_id = 0;
    chat::MeshProtocol protocol = chat_support::active_mesh_protocol();
    std::string title;
    chat::ReticulumPeerIdentity reticulum_destination{};
    if (g_contacts_state.current_mode == ContactsMode::Broadcast)
    {
        BroadcastTargetSpec target_spec{};
        std::string target_title;
        if (!get_selected_broadcast_target(&target_spec, &target_title))
        {
            return;
        }
        if (!target_spec.chat_supported)
        {
            ::ui::feedback::show_notice(broadcast_chat_unavailable_message(target_spec), 2200);
            return;
        }
        protocol = target_spec.protocol;
        channel = target_spec.channel;
        peer_id = 0;
        title = target_title.empty() ? "Broadcast" : target_title;
    }
    else if (g_contacts_state.current_mode == ContactsMode::Groups)
    {
        if (!chat_support::supports_reticulum_destination_text())
        {
            ::ui::feedback::show_notice(
                chat_support::reticulum_destination_text_unavailable_message(),
                2200);
            return;
        }
        protocol = chat::MeshProtocol::Reticulum;
        channel = chat::ChannelId::PRIMARY;
        peer_id = 0;
        reticulum_destination = group->reticulum_identity;
        title = group->display_name.empty() ? "Group" : group->display_name;
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        if (!chat_support::supports_team_chat())
        {
            ::ui::feedback::show_notice(chat_support::team_chat_unavailable_message(), 2200);
            return;
        }
        // Preserve the virtual Team identity for Chat's workflow selection.
        // The Team workflow separately chooses its physical bearer channel.
        channel = chat::ChannelId::TEAM;
        peer_id = 0;
        title = contacts_team_title();
    }
    else
    {
        channel = chat::ChannelId::PRIMARY;
        peer_id = node->node_id;
        chat::MeshProtocol node_protocol = protocol;
        if (node_protocol_to_mesh(node->protocol, &node_protocol) &&
            node_protocol != protocol)
        {
            const std::string msg =
                ::ui::i18n::format("Switch to %s to chat",
                                   chat::infra::meshProtocolName(node_protocol));
            ::ui::feedback::show_notice(msg.c_str(), 2200);
            return;
        }
        const bool use_reticulum_destination =
            protocol == chat::MeshProtocol::Reticulum &&
            chat::hasReticulumDestinationIdentity(node->reticulum_identity);
        const bool text_supported =
            use_reticulum_destination
                ? chat_support::supports_reticulum_destination_text()
                : chat_support::supports_local_text_chat();
        if (!text_supported)
        {
            ::ui::feedback::show_notice(
                use_reticulum_destination
                    ? chat_support::reticulum_destination_text_unavailable_message()
                    : chat_support::local_text_chat_unavailable_message(),
                2200);
            return;
        }
        if (use_reticulum_destination)
        {
            peer_id = 0;
            reticulum_destination = node->reticulum_identity;
        }
        if (g_contacts_state.contact_service)
        {
            title = g_contacts_state.contact_service->getContactName(node->node_id);
        }
        if (title.empty())
        {
            title = node_display_name_for_contacts(*node);
        }
    }

    chat::ConversationId conv(channel, peer_id, protocol);
    if (chat::hasReticulumDestinationIdentity(reticulum_destination))
    {
        conv.reticulum_identity = reticulum_destination;
    }
    else if (node && protocol == chat::MeshProtocol::Reticulum &&
             chat::hasReticulumDestinationIdentity(node->reticulum_identity))
    {
        conv.reticulum_identity = node->reticulum_identity;
    }

    // Contacts owns target selection only.  Compose, its focus group, VMP
    // affordances, durable voice state, and post-send return behaviour are
    // all owned by the Chat app.  Never construct a second Compose tree here.
    if (!chat::ui::runtime::requestCompose(conv))
    {
        std::printf("[Contacts][Route] chat_compose rejected reason=chat_runtime_unavailable\n");
        ::ui::feedback::show_notice("Chat unavailable", 1800);
        return;
    }
    if (!::ui::menu_layout::launchAppByStableId("chat"))
    {
        chat::ui::runtime::clearRequestedCompose();
        std::printf("[Contacts][Route] chat_compose rejected reason=chat_app_unavailable\n");
        ::ui::feedback::show_notice("Chat unavailable", 1800);
        return;
    }
    char routed_dest_hash[12] = {};
    format_reticulum_hash_prefix(conv.reticulum_identity,
                                 routed_dest_hash,
                                 sizeof(routed_dest_hash));
    std::printf("[Contacts][Route] chat_compose protocol=%s group=%u target=\"%s\" ch=%u peer=%08lX dest=%s\n",
                chat::infra::meshProtocolName(protocol),
                g_contacts_state.current_mode == ContactsMode::Groups ? 1U : 0U,
                title.c_str(),
                static_cast<unsigned>(channel),
                static_cast<unsigned long>(peer_id),
                routed_dest_hash);
}

static void refresh_team_conversation()
{
    if (!g_contacts_state.conversation_screen || !is_team_available())
    {
        return;
    }
    g_contacts_state.conversation_screen->clearMessages();

    ::ui::chat::ChatWorkspaceRequest request;
    ::ui::chat::ChatWorkspaceSnapshot snapshot;
    if (contacts_team_chat_source().buildChatWorkspaceSnapshot(request, snapshot) &&
        snapshot.conversation_count > 0)
    {
        request.selected = snapshot.conversations[0].id;
        if (contacts_team_chat_source().buildChatWorkspaceSnapshot(request, snapshot))
        {
            for (size_t index = 0; index < snapshot.message_count; ++index)
            {
                g_contacts_state.conversation_screen->addMessage(
                    snapshot.messages[index]);
            }
        }
    }
    g_contacts_state.conversation_screen->scrollToBottom();
}

static void on_team_conversation_action(chat::ui::ChatConversationScreen::ActionIntent intent, void* /*user_data*/)
{
    if (intent == chat::ui::ChatConversationScreen::ActionIntent::Reply)
    {
        open_chat_compose();
    }
}

static void on_team_conversation_back(void* /*user_data*/)
{
    close_team_conversation();
}

[[maybe_unused]] static void open_team_conversation()
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
    if (lv_obj_t* chat_parent = chat::ui::shell::get_container())
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

    const chat::MeshProtocol protocol = app::appFacade().readConfig().mesh_protocol;
    chat::ConversationId conv(chat::ChannelId::PRIMARY, 0, protocol);
    g_contacts_state.conversation_screen = new chat::ui::ChatConversationScreen(parent, conv);
    g_contacts_state.conversation_screen->setActionCallback(on_team_conversation_action, nullptr);
    g_contacts_state.conversation_screen->setBackCallback(on_team_conversation_back, nullptr);

    const std::string title = contacts_team_title();
    g_contacts_state.conversation_screen->setHeaderText(title.c_str(), nullptr);
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
    contacts::ui::ContactsTeamSnapshot team_snapshot;
    if (!load_contacts_team_snapshot(team_snapshot))
    {
        return;
    }
    app::IAppFacade& app_ctx = app::appFacade();
    team::TeamController* controller = app_ctx.getTeamController();
    if (!controller)
    {
        return;
    }

    ::gps::GpsState gps_state = platform::ui::gps::get_data();
    if (!gps_state.valid)
    {
        CONTACTS_LOG("[Contacts] team position: no gps fix\n");
        return;
    }

    int32_t lat_e7 = static_cast<int32_t>(gps_state.lat * 1e7);
    int32_t lon_e7 = static_cast<int32_t>(gps_state.lng * 1e7);
    uint32_t ts = current_timestamp_seconds();

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
        pos.sats_in_view = gps_state.satellites;
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
        team::ui::team_ui_posring_append(team_snapshot.team_id,
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
        ::ui::i18n::set_label_text(g_contacts_state.add_edit_error_label, "Name required");
        lv_obj_clear_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (strlen(nickname) > 12)
    {
        ::ui::i18n::set_label_text(g_contacts_state.add_edit_error_label, "Name too long");
        lv_obj_clear_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!g_contacts_state.contact_service)
    {
        return;
    }
    chat::ReticulumPeerIdentity reticulum_identity{};
    bool add_reticulum_contact = false;
    if (!g_contacts_state.modal_is_edit)
    {
        if (const auto* node = find_node_by_id(g_contacts_state.modal_node_id))
        {
            add_reticulum_contact =
                is_reticulum_node(*node) &&
                chat::hasReticulumDestinationIdentity(node->reticulum_identity);
            if (add_reticulum_contact)
            {
                reticulum_identity = node->reticulum_identity;
            }
        }
    }

    std::vector<chat::contacts::PeerBrowseEntry> matches;
    if (!g_contacts_state.contact_service->browsePeers(chat::MeshPeerDirectoryView::Contacts, nickname, matches).succeeded())
    {
        ::ui::i18n::set_label_text(g_contacts_state.add_edit_error_label, "Contacts unavailable");
        lv_obj_clear_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    chat::contacts::PeerDirectoryItem candidate{};
    for (const auto& entry : matches)
    {
        if (g_contacts_state.card_index < g_contacts_state.card_entries.size() &&
            entry.samePeer(g_contacts_state.card_entries[g_contacts_state.card_index]))
        {
            continue;
        }
        if (g_contacts_state.contact_service->readPeer(entry, candidate).succeeded() && candidate.display_name == nickname)
        {
            ::ui::i18n::set_label_text(g_contacts_state.add_edit_error_label, "Duplicate name not allowed");
            lv_obj_clear_flag(g_contacts_state.add_edit_error_label, LV_OBJ_FLAG_HIDDEN);
            return;
        }
    }

    ScopedReticulumContactSaveOverlay save_overlay(
        !g_contacts_state.modal_is_edit && add_reticulum_contact,
        "Updating SD address book");
    bool ok = false;
    if (g_contacts_state.modal_is_edit)
    {
        ok = g_contacts_state.contact_service->editContact(g_contacts_state.modal_node_id, nickname);
    }
    else
    {
        ok = g_contacts_state.contact_service->addContact(g_contacts_state.modal_node_id, nickname);
    }

    chat::MeshActionResult reticulum_persist_result{};
    bool reticulum_persist_attempted = false;
    if (!g_contacts_state.modal_is_edit && add_reticulum_contact)
    {
        reticulum_persist_attempted = true;
        reticulum_persist_result =
            persist_reticulum_contact_peer(reticulum_identity, true);
        if (reticulum_persist_result.ok)
        {
            ok = true;
        }
    }

    if (!ok)
    {
        ::ui::i18n::set_label_text(g_contacts_state.add_edit_error_label, "Save failed");
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
    if (reticulum_persist_attempted && !reticulum_persist_result.ok)
    {
        ::ui::feedback::show_notice(
            reticulum_address_save_failure_message(reticulum_persist_result.failure),
            1800);
    }
    else if (reticulum_persist_attempted)
    {
        ::ui::feedback::show_notice("Contact saved", 1400);
    }
    contacts_focus_to_list();
}

static void on_add_edit_cancel_clicked(lv_event_t* /*e*/)
{
    g_contacts_state.add_edit_textarea = nullptr;
    g_contacts_state.add_edit_error_label = nullptr;
    modal_close(g_contacts_state.add_edit_modal);
    contacts_focus_to_list();
}

static void on_reticulum_group_save_clicked(lv_event_t* /*e*/)
{
    if (!g_contacts_state.reticulum_group_name_textarea ||
        !g_contacts_state.reticulum_group_destination_textarea)
    {
        return;
    }

    const char* name = lv_textarea_get_text(g_contacts_state.reticulum_group_name_textarea);
    const char* destination =
        lv_textarea_get_text(g_contacts_state.reticulum_group_destination_textarea);
    if (!name || name[0] == '\0')
    {
        show_reticulum_group_error("Name required");
        return;
    }

    chat::ReticulumPeerIdentity identity{};
    char error[96] = {};
    if (!chat::parseReticulumDestinationHashText(destination,
                                                 &identity,
                                                 error,
                                                 sizeof(error)))
    {
        show_reticulum_group_error(error[0] != '\0' ? error : "Invalid destination");
        return;
    }

    if (!reticulum_group_storage_ready_for_edit())
    {
        show_reticulum_group_error(
            g_contacts_state.reticulum_group_storage_message[0] != '\0'
                ? g_contacts_state.reticulum_group_storage_message
                : "Reticulum required");
        return;
    }

    app::AppConfigEdit edit = app::configFacade().beginConfigEdit();
    if (!edit)
    {
        show_reticulum_group_error("Configuration busy");
        return;
    }
    auto& groups = edit.config().reticulumConfig().reticulum_groups;

    int free_slot = -1;
    for (std::size_t index = 0; index < chat::kReticulumGroupDestinationMaxCount; ++index)
    {
        auto& group = groups[index];
        if (group.enabled && chat::hasReticulumDestinationIdentity(group.identity) &&
            chat::sameReticulumDestinationHash(group.identity, identity))
        {
            show_reticulum_group_error("Group already exists");
            return;
        }
        if (free_slot < 0 &&
            (!group.enabled || !chat::hasReticulumDestinationIdentity(group.identity)))
        {
            free_slot = static_cast<int>(index);
        }
    }

    if (free_slot < 0)
    {
        show_reticulum_group_error("Group list full");
        return;
    }

    auto& group = groups[static_cast<std::size_t>(free_slot)];
    group = chat::ReticulumGroupDestinationConfig{};
    group.enabled = true;
    std::snprintf(group.name, sizeof(group.name), "%s", name);
    group.identity = identity;

    edit.commit(app::AppConfigChangeSet::mesh());
    app::configFacade().applyMeshConfig();

    g_contacts_state.reticulum_group_name_textarea = nullptr;
    g_contacts_state.reticulum_group_destination_textarea = nullptr;
    g_contacts_state.reticulum_group_error_label = nullptr;
    modal_close(g_contacts_state.reticulum_group_modal);

    g_contacts_state.current_mode = ContactsMode::Groups;
    g_contacts_state.current_page = 0;
    g_contacts_state.selected_index = -1;
    refresh_contacts_data();
    refresh_ui();
    contacts_focus_to_list();
    ::ui::feedback::show_notice("Reticulum group saved", 1600);
}

static void on_reticulum_group_cancel_clicked(lv_event_t* /*e*/)
{
    g_contacts_state.reticulum_group_name_textarea = nullptr;
    g_contacts_state.reticulum_group_destination_textarea = nullptr;
    g_contacts_state.reticulum_group_error_label = nullptr;
    modal_close(g_contacts_state.reticulum_group_modal);
    contacts_focus_to_list();
}

static void on_del_confirm_clicked(lv_event_t* /*e*/)
{
    chat::ReticulumPeerIdentity reticulum_identity{};
    bool reticulum_contact = false;
    if (const auto* node = find_node_by_id(g_contacts_state.modal_node_id))
    {
        reticulum_contact =
            is_reticulum_node(*node) &&
            chat::hasReticulumDestinationIdentity(node->reticulum_identity);
        if (reticulum_contact)
        {
            reticulum_identity = node->reticulum_identity;
        }
    }

    ScopedReticulumContactSaveOverlay save_overlay(reticulum_contact,
                                                   "Updating SD address book");
    if (g_contacts_state.contact_service)
    {
        g_contacts_state.contact_service->removeContact(g_contacts_state.modal_node_id);
    }
    if (reticulum_contact)
    {
        const auto favorite_status =
            rtdir::set_lxmf_address_favorite_now(reticulum_identity.destination_hash, false);
        if (favorite_status.sd_present && !favorite_status.saved)
        {
            std::printf("[Contacts][RT] favorite_clear failed message=%s detail=%s\n",
                        favorite_status.message,
                        favorite_status.detail);
        }
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

static void on_node_info_key(lv_event_t* e)
{
    if (lv_event_get_code(e) != LV_EVENT_KEY)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(e);
    if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
    {
        CONTACTS_NODE_INFO_LOG("back key received key=%lu\n", static_cast<unsigned long>(key));
        close_node_info_screen();
    }
}

static const char* discovery_failure_message(chat::MeshOperationFailure failure,
                                             const char* fallback)
{
    switch (failure)
    {
    case chat::MeshOperationFailure::NotReady:
        return "Mesh not ready";
    case chat::MeshOperationFailure::TxDisabled:
        return "TX disabled";
    case chat::MeshOperationFailure::RadioOffline:
        return "Radio offline";
    case chat::MeshOperationFailure::DutyCycleLimited:
        return "TX rate limited";
    case chat::MeshOperationFailure::LocalIdentityMissing:
        return "Identity missing";
    case chat::MeshOperationFailure::Busy:
        return "Radio busy";
    case chat::MeshOperationFailure::RadioTxFailed:
        return "Radio TX failed";
    case chat::MeshOperationFailure::EncodeFailed:
        return "Packet build failed";
    case chat::MeshOperationFailure::CryptoFailed:
        return "Signature failed";
    case chat::MeshOperationFailure::Unsupported:
        return "Unsupported";
    case chat::MeshOperationFailure::InvalidInput:
        return "Invalid action";
    case chat::MeshOperationFailure::PeerKeyMissing:
        return "Peer key missing";
    case chat::MeshOperationFailure::ChannelKeyMissing:
        return "Channel key missing";
    case chat::MeshOperationFailure::None:
    case chat::MeshOperationFailure::Unknown:
        break;
    }
    return fallback;
}

static size_t nearby_peer_count()
{
    std::vector<chat::contacts::PeerBrowseEntry> entries;
    auto& service = app::messagingFacade().getContactService();
    if (!service.browsePeers(chat::MeshPeerDirectoryView::Nearby, "", entries).succeeded()) return 0;
    return entries.size();
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

    if (chat_support::active_mesh_protocol() != chat::MeshProtocol::MeshCore || !g_contacts_state.chat_service)
    {
        ::ui::feedback::show_notice("MeshCore only", 2000);
        return;
    }

    if (spec.command == DiscoveryActionCommand::ScanLocal)
    {
        refresh_contacts_data();
        g_contacts_state.discover_scan_start_nearby = nearby_peer_count();
        if (g_contacts_state.discover_scan_timer)
        {
            lv_timer_del(g_contacts_state.discover_scan_timer);
            g_contacts_state.discover_scan_timer = nullptr;
        }
        const chat::MeshActionResult result =
            g_contacts_state.chat_service->triggerDiscoveryActionDetailed(
                chat::MeshDiscoveryAction::ScanLocal);
        if (!result.ok)
        {
            ::ui::feedback::show_notice(
                discovery_failure_message(result.failure, "Scan failed"),
                2000);
            return;
        }
        ::ui::feedback::show_notice("Scanning 5s...", 1800);
        g_contacts_state.discover_scan_timer = lv_timer_create(on_discovery_scan_done, 5000, nullptr);
        lv_timer_set_repeat_count(g_contacts_state.discover_scan_timer, 1);
        return;
    }

    const chat::MeshDiscoveryAction action =
        (spec.command == DiscoveryActionCommand::SendIdLocal)
            ? chat::MeshDiscoveryAction::SendIdLocal
            : chat::MeshDiscoveryAction::SendIdBroadcast;
    const chat::MeshActionResult result =
        g_contacts_state.chat_service->triggerDiscoveryActionDetailed(action);
    if (result.ok)
    {
        ::ui::feedback::show_notice(
            (spec.command == DiscoveryActionCommand::SendIdLocal) ? "ID local sent" : "ID bcast sent",
            2000);
    }
    else
    {
        ::ui::feedback::show_notice(
            discovery_failure_message(
                result.failure,
                (spec.command == DiscoveryActionCommand::SendIdLocal) ? "ID local fail" : "ID bcast fail"),
            2000);
    }
}

static void on_discovery_scan_done(lv_timer_t* timer)
{
    const size_t start_count = g_contacts_state.discover_scan_start_nearby;
    refresh_contacts_data();
    refresh_ui();
    const size_t total = nearby_peer_count();
    const size_t gained = (total > start_count) ? (total - start_count) : 0;

    const std::string msg =
        ::ui::i18n::format("Scan +%u/%u",
                           static_cast<unsigned>(gained),
                           static_cast<unsigned>(total));
    ::ui::feedback::show_notice(msg.c_str(), 2200);

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
    ToggleIgnore = 7,
    Ping = 8,
    Call = 9,
    Cancel = 10,
};

static const char* reticulum_ping_failure_message(const chat::MeshActionResult& result)
{
    if (result.failure == chat::MeshOperationFailure::NotReady && result.detail == 1)
    {
        return "Path requested";
    }
    if (result.failure == chat::MeshOperationFailure::NotReady && result.detail == 2)
    {
        return "Path pending";
    }
    switch (result.failure)
    {
    case chat::MeshOperationFailure::InvalidInput:
        return "Ping unavailable";
    case chat::MeshOperationFailure::Unsupported:
        return "Ping unsupported";
    case chat::MeshOperationFailure::NotReady:
        return "Radio not ready";
    case chat::MeshOperationFailure::PeerKeyMissing:
        return "Peer identity missing";
    case chat::MeshOperationFailure::CryptoFailed:
        return "Ping setup failed";
    case chat::MeshOperationFailure::RadioTxFailed:
        return "Ping send failed";
    case chat::MeshOperationFailure::TxDisabled:
        return "TX disabled";
    case chat::MeshOperationFailure::RadioOffline:
        return "Radio offline";
    case chat::MeshOperationFailure::DutyCycleLimited:
        return "TX rate limited";
    case chat::MeshOperationFailure::Busy:
        return "Radio busy";
    case chat::MeshOperationFailure::ChannelKeyMissing:
        return "Key missing";
    case chat::MeshOperationFailure::EncodeFailed:
    case chat::MeshOperationFailure::LocalIdentityMissing:
    case chat::MeshOperationFailure::None:
    case chat::MeshOperationFailure::Unknown:
        break;
    }
    return "Ping failed";
}

static const char* reticulum_call_failure_message(const chat::MeshActionResult& result)
{
    if (result.failure == chat::MeshOperationFailure::NotReady && result.detail == 1)
    {
        return "Path requested";
    }
    switch (result.failure)
    {
    case chat::MeshOperationFailure::InvalidInput:
        return "Invalid peer";
    case chat::MeshOperationFailure::Unsupported:
        return "Call unsupported";
    case chat::MeshOperationFailure::NotReady:
        return "Wi-Fi gateway unavailable";
    case chat::MeshOperationFailure::LocalIdentityMissing:
        return "Identity missing";
    case chat::MeshOperationFailure::PeerKeyMissing:
        return "Peer identity missing";
    case chat::MeshOperationFailure::Busy:
        return "Call already active";
    case chat::MeshOperationFailure::EncodeFailed:
    case chat::MeshOperationFailure::CryptoFailed:
        return "Call setup failed";
    case chat::MeshOperationFailure::RadioTxFailed:
        return "Gateway send failed";
    case chat::MeshOperationFailure::TxDisabled:
        return "TX disabled";
    case chat::MeshOperationFailure::RadioOffline:
        return "Radio offline";
    case chat::MeshOperationFailure::DutyCycleLimited:
        return "TX rate limited";
    case chat::MeshOperationFailure::ChannelKeyMissing:
        return "Key missing";
    case chat::MeshOperationFailure::None:
    case chat::MeshOperationFailure::Unknown:
        break;
    }
    return "Call failed";
}

static bool selected_node_supports_reticulum_call(const chat::contacts::PeerDirectoryItem* node)
{
    if (!node || chat_support::active_mesh_protocol() != chat::MeshProtocol::Reticulum)
    {
        return false;
    }
    if (g_contacts_state.current_mode != ContactsMode::Contacts &&
        g_contacts_state.current_mode != ContactsMode::Nearby &&
        g_contacts_state.current_mode != ContactsMode::Ignored)
    {
        return false;
    }
    return chat_support::supports_reticulum_audio_call() &&
           chat::hasReticulumDestinationIdentity(node->reticulum_identity);
}

static bool selected_node_supports_reticulum_ping(const chat::contacts::PeerDirectoryItem* node)
{
    if (!node || chat_support::active_mesh_protocol() != chat::MeshProtocol::Reticulum)
    {
        return false;
    }
    if (g_contacts_state.current_mode != ContactsMode::Contacts &&
        g_contacts_state.current_mode != ContactsMode::Nearby &&
        g_contacts_state.current_mode != ContactsMode::Ignored)
    {
        return false;
    }
    return chat_support::supports_reticulum_destination_ping() &&
           chat::hasReticulumDestinationIdentity(node->reticulum_identity);
}

static void start_reticulum_ping_for_selected_node()
{
    const auto* node = get_selected_node();
    if (!selected_node_supports_reticulum_ping(node) || !g_contacts_state.chat_service)
    {
        ::ui::feedback::show_notice("Ping unavailable", 1800);
        contacts_focus_to_list();
        return;
    }

    ::ui::widgets::reticulum_ping::show_loading(
        node->reticulum_identity.destination_hash,
        node->display_name.empty() ? node->long_name : node->display_name.c_str());
    const chat::MeshActionResult result =
        g_contacts_state.chat_service->pingReticulumDestination(node->reticulum_identity);
    if (!result.ok)
    {
        ::ui::widgets::reticulum_ping::show_send_failure(
            reticulum_ping_failure_message(result));
    }
    contacts_focus_to_list();
}

static void start_reticulum_call_for_selected_node()
{
    const auto* node = get_selected_node();
    if (!selected_node_supports_reticulum_call(node) || !g_contacts_state.chat_service)
    {
        ::ui::feedback::show_notice("Call unavailable", 1800);
        contacts_focus_to_list();
        return;
    }

    const chat::MeshActionResult result =
        g_contacts_state.chat_service->startReticulumAudioCall(node->reticulum_identity);
    if (result.ok)
    {
        const std::string name = node_display_name_for_contacts(*node);
        const std::string msg = name.empty()
                                    ? std::string("Calling")
                                    : ::ui::i18n::format("Calling %s", name.c_str());
        ::ui::feedback::show_notice(msg.c_str(), 1600);
    }
    else if (result.failure == chat::MeshOperationFailure::NotReady && result.detail == 1)
    {
        ::ui::feedback::show_notice("Path requested", 2200);
    }
    else
    {
        ::ui::feedback::show_notice(reticulum_call_failure_message(result), 2200);
    }
    contacts_focus_to_list();
}

static void toggle_selected_node_ignore()
{
    const auto* node = get_selected_node();
    if (!node || !g_contacts_state.contact_service)
    {
        ::ui::feedback::show_notice("Ignore unavailable", 1800);
        contacts_focus_to_list();
        return;
    }

    const bool ignored = !node->is_ignored;
    const bool node_is_contact = node->is_contact;
    if (!g_contacts_state.contact_service->setNodeIgnored(node->node_id, ignored))
    {
        ::ui::feedback::show_notice("Ignore update failed", 1800);
        contacts_focus_to_list();
        return;
    }

    refresh_contacts_data();
    refresh_ui();
    if (ignored && g_contacts_state.current_mode == ContactsMode::Nearby && !node_is_contact)
    {
        ::ui::feedback::show_notice("Node ignored and hidden", 2000);
    }
    else
    {
        ::ui::feedback::show_notice(ignored ? "Node ignored" : "Node unignored", 1800);
    }
    contacts_focus_to_list();
}

static lv_obj_t* create_action_menu_button(lv_obj_t* parent, const char* text)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), page_button_height());
    contacts::ui::style::apply_btn_basic(btn);
    // Keep default neutral background and highlight strictly by focus state.
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorPanelBg), selector_for_state(LV_STATE_DEFAULT));
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorAmber), selector_for_state(LV_STATE_FOCUSED));
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorAmber), selector_for_state(LV_STATE_FOCUS_KEY));
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColorAmberDark), selector_for_state(LV_STATE_PRESSED));

    lv_obj_t* label = lv_label_create(btn);
    ::ui::i18n::set_label_text(label, text);
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

    uint32_t selected_node_id = 0;
    if (const auto* node = get_selected_node())
    {
        selected_node_id = node->node_id;
    }

    modal_close(g_contacts_state.action_menu_modal);

    switch (cmd)
    {
    case ActionMenuCommand::Chat:
        open_chat_compose();
        break;
    case ActionMenuCommand::Position:
        send_team_position();
        break;
    case ActionMenuCommand::Info:
        if (selected_node_id != 0)
        {
            open_node_info_screen_for_node(selected_node_id);
        }
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
    case ActionMenuCommand::ToggleIgnore:
        toggle_selected_node_ignore();
        break;
    case ActionMenuCommand::Ping:
        start_reticulum_ping_for_selected_node();
        break;
    case ActionMenuCommand::Call:
        start_reticulum_call_for_selected_node();
        break;
    case ActionMenuCommand::Cancel:
    default:
        contacts_focus_to_list();
        break;
    }
}

static void open_action_menu_modal()
{
    if (is_any_modal_open())
    {
        return;
    }
    if (g_contacts_state.current_mode == ContactsMode::Discover ||
        g_contacts_state.current_mode == ContactsMode::Public)
    {
        return;
    }
    if (g_contacts_state.selected_index < 0)
    {
        return;
    }

    const chat::contacts::PeerDirectoryItem* node = get_selected_node();
    const bool show_ignore = (node != nullptr) &&
                             (g_contacts_state.current_mode == ContactsMode::Contacts ||
                              g_contacts_state.current_mode == ContactsMode::Nearby);
    const bool allow_reticulum_call = selected_node_supports_reticulum_call(node);
    const bool allow_reticulum_ping = selected_node_supports_reticulum_ping(node);

    const bool allow_chat_action =
        (g_contacts_state.current_mode == ContactsMode::Team)
            ? chat_support::supports_team_chat()
        : (g_contacts_state.current_mode == ContactsMode::Groups)
            ? chat_support::supports_reticulum_destination_text()
        : (node && chat_support::active_mesh_protocol() == chat::MeshProtocol::Reticulum &&
           chat::hasReticulumDestinationIdentity(node->reticulum_identity))
            ? chat_support::supports_reticulum_destination_text()
            : chat_support::supports_local_text_chat();
    int action_count = allow_chat_action ? 2 : 1; // Chat + Cancel
    if (allow_reticulum_call)
    {
        action_count += 1;
    }
    if (allow_reticulum_ping)
    {
        action_count += 1;
    }
    if (g_contacts_state.current_mode == ContactsMode::Contacts)
    {
        action_count += 3; // Edit/Delete/Info
    }
    else if (g_contacts_state.current_mode == ContactsMode::Nearby ||
             g_contacts_state.current_mode == ContactsMode::Ignored)
    {
        action_count += 2; // Add/Info
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        action_count += 1; // Position
    }
    if (show_ignore)
    {
        action_count += 1;
    }

    int modal_h = (::ui::page_profile::current().large_touch_hitbox ? 84 : 62) + action_count * (page_button_height() + (::ui::page_profile::current().large_touch_hitbox ? 8 : 2));
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

    std::string title = ::ui::i18n::tr("Actions");
    if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        title = ::ui::i18n::tr("Team Actions");
    }
    else if (g_contacts_state.current_mode == ContactsMode::Groups)
    {
        title = ::ui::i18n::tr("Group Actions");
        if (const auto* group = get_selected_reticulum_group())
        {
            const std::string name = group->display_name.empty()
                                         ? std::string(group->long_name)
                                         : group->display_name;
            if (!name.empty())
            {
                title = ::ui::i18n::format("Actions: %s", name.c_str());
            }
        }
    }
    else if (g_contacts_state.current_mode == ContactsMode::Broadcast)
    {
        title = ::ui::i18n::tr("Channel Actions");
    }
    else if (const auto* node = get_selected_node())
    {
        std::string name = node_display_name_for_contacts(*node);
        if (!name.empty())
        {
            title = ::ui::i18n::format("Actions: %s", name.c_str());
        }
    }

    // Use flex layout to avoid stale height reads causing a too-short action list
    // on some devices (e.g. T-Deck). Let the list container grow to fill window.
    lv_obj_set_flex_flow(win, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(win, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(win, 4, LV_PART_MAIN);

    lv_obj_t* title_label = lv_label_create(win);
    ::ui::i18n::set_label_text_raw(title_label, title.c_str());
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
    lv_obj_set_style_min_height(list, page_button_height() + (::ui::page_profile::current().large_touch_hitbox ? 10 : 4), LV_PART_MAIN);
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

    if (allow_chat_action)
    {
        add_action(ActionMenuCommand::Chat, "Chat");
    }
    if (allow_reticulum_ping)
    {
        add_action(ActionMenuCommand::Ping, "Ping");
    }
    if (allow_reticulum_call)
    {
        add_action(ActionMenuCommand::Call, "Call");
    }
    if (g_contacts_state.current_mode == ContactsMode::Contacts)
    {
        add_action(ActionMenuCommand::Edit, "Edit");
        add_action(ActionMenuCommand::Delete, "Delete");
        add_action(ActionMenuCommand::Info, "Info");
    }
    else if (g_contacts_state.current_mode == ContactsMode::Nearby ||
             g_contacts_state.current_mode == ContactsMode::Ignored)
    {
        add_action(ActionMenuCommand::Add, "Add");
        add_action(ActionMenuCommand::Info, "Info");
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        add_action(ActionMenuCommand::Position, "Position");
    }
    if (show_ignore)
    {
        add_action(ActionMenuCommand::ToggleIgnore,
                   (node && node->is_ignored) ? "Unignore" : "Ignore");
    }
    add_action(ActionMenuCommand::Cancel, "Cancel");

    if (first_focus)
    {
        lv_group_focus_obj(first_focus);
    }
}

// ---------------- UI refresh (public API) ----------------

static void step_node_card(int direction)
{
    auto& state = g_contacts_state;
    if (state.card_entries.empty()) return;
    const int next = static_cast<int>(state.card_index) + direction;
    if (next < 0 || next >= static_cast<int>(state.card_entries.size())) return;
    state.card_index = static_cast<size_t>(next);
    hydrate_node_card();
    refresh_ui();
}

static void on_card_step(lv_event_t* event)
{
    step_node_card(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))));
}

static void on_card_details(lv_event_t*)
{
    const auto* peer = get_selected_node();
    if (!peer) return;
    if (is_reticulum_node(*peer)) open_reticulum_node_info_screen(*peer, g_contacts_state.root);
    else open_node_info_screen_for_node(peer->node_id);
}

static void on_card_navigation(lv_event_t* event)
{
    if (lv_event_get_code(event) == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(event);
        if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT) step_node_card(1);
        else if (key == LV_KEY_UP || key == LV_KEY_LEFT) step_node_card(-1);
        else if (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE)
        {
            lv_event_stop_processing(event);
            lv_event_stop_bubbling(event);
            // The filter menu already owns its first-level dismissal. Back
            // from the card must leave Contacts, not reopen that same menu.
            if (g_contacts_state.top_bar.back_btn)
                lv_obj_send_event(g_contacts_state.top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
        }
    }
    else if (lv_event_get_code(event) == LV_EVENT_GESTURE)
    {
        auto* input = lv_indev_active();
        if (!input) return;
        const auto direction = lv_indev_get_gesture_dir(input);
        if (direction == LV_DIR_TOP) step_node_card(1);
        else if (direction == LV_DIR_BOTTOM) step_node_card(-1);
    }
}

static void on_card_actions(lv_event_t* event)
{
    if (lv_event_get_target(event) == g_contacts_state.node_card && g_contacts_state.card_peer_valid)
        on_list_item_clicked(event);
}

static void on_footer_back(lv_event_t*)
{
    if (g_contacts_state.top_bar.back_btn)
        lv_obj_send_event(g_contacts_state.top_bar.back_btn, LV_EVENT_CLICKED, nullptr);
}

static void refresh_node_card_ui()
{
    auto& state = g_contacts_state;
    if (!state.card_context_valid || state.card_mode != state.current_mode ||
        std::strcmp(state.card_query, state.search_query) != 0) refresh_node_card_data();
    const bool created = !state.node_card;
    if (created)
    {
        clear_footer_actions();
        lv_obj_clean(state.list_panel);
        state.sub_container = nullptr;
        state.empty_label = nullptr;
        state.list_items.clear();
        state.prev_btn = state.next_btn = state.back_btn = nullptr;
        const auto view = card_view::create(state.list_panel, ::ui::page_profile::resolve_body_font(),
                                            ::ui::page_profile::resolve_caption_font(), ::ui::page_profile::resolve_title_font(),
                                            state.bottom_container);
        auto* card = view.root;
        state.node_card = view.root;
        state.card_name = view.name;
        state.card_identity = view.identity;
        state.card_region = view.region;
        state.card_distance = view.distance;
        state.card_status = view.status;
        state.card_counter = view.counter;
        state.prev_btn = view.previous;
        state.next_btn = view.next;
        auto* details = view.details;
        state.card_details_btn = details;
        for (auto* button : {state.prev_btn, state.next_btn, details})
        {
            lv_obj_set_parent(button, state.bottom_container);
            ::ui::components::bottom_bar::style_button(button);
            lv_obj_set_size(button, 48, ::ui::components::bottom_bar::kButtonHeight);
        }
        lv_obj_set_height(lv_obj_get_parent(view.counter), LV_SIZE_CONTENT);
        ::ui::i18n::set_label_text(lv_obj_get_child(state.prev_btn, 0), "Prev");
        ::ui::i18n::set_label_text(lv_obj_get_child(state.next_btn, 0), "Next");
        ::ui::i18n::set_label_text(lv_obj_get_child(details, 0), "Info");
        lv_obj_set_user_data(card, nullptr);
        lv_obj_add_event_cb(card, on_card_actions, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(card, on_card_navigation, LV_EVENT_KEY, nullptr);
        lv_obj_add_event_cb(card, on_card_navigation, LV_EVENT_GESTURE, nullptr);
        bind_page_shortcuts(card);
        lv_obj_add_event_cb(state.prev_btn, on_card_step, LV_EVENT_CLICKED, reinterpret_cast<void*>(-1));
        lv_obj_add_event_cb(state.next_btn, on_card_step, LV_EVENT_CLICKED, reinterpret_cast<void*>(1));
        lv_obj_add_event_cb(details, on_card_details, LV_EVENT_CLICKED, nullptr);
        state.back_btn = create_bottom_bar_button(state.bottom_container, "Back", kColorPanelBg, on_footer_back);
        bind_page_shortcuts(state.back_btn);
        state.list_items = {card, state.prev_btn, state.next_btn, details};
        arrange_footer_actions();
    }
    char text[96]{};
    const auto& peer = state.card_peer;
    if (!state.card_peer_valid)
    {
        ::ui::i18n::set_label_text(state.card_name, state.contacts_data_hydration_pending ? "Loading..." : "No nodes");
        lv_label_set_text(state.card_identity, "");
        lv_label_set_text(state.card_region, "");
        lv_label_set_text(state.card_distance, "");
        lv_label_set_text(state.card_status, "");
    }
    else
    {
        ::ui::i18n::set_content_label_text_raw(state.card_name, layout::preferred_node_display_name(peer).c_str());
        const auto& entry = state.card_entries[state.card_index];
        if (entry.kind == chat::MeshPeerIdentityKind::NodeId)
            std::snprintf(text, sizeof(text), "MT !%08lx", static_cast<unsigned long>(entry.node_id));
        else
            std::snprintf(text, sizeof(text), "%s %02x%02x%02x...%02x%02x",
                          entry.kind == chat::MeshPeerIdentityKind::PublicKey ? "MC Key" : "RT Dest",
                          entry.identity_bytes[0], entry.identity_bytes[1], entry.identity_bytes[2],
                          entry.identity_bytes[entry.identity_size - 2], entry.identity_bytes[entry.identity_size - 1]);
        lv_label_set_text(state.card_identity, text);
        namespace map_search = ::platform::ui::map_search;
        if (peer.position.valid)
        {
            const bool moved = !state.card_region_coordinate_valid ||
                               state.card_region_latitude != peer.position.latitude_i ||
                               state.card_region_longitude != peer.position.longitude_i;
            if (moved)
            {
                state.card_region_request = 0;
                state.card_region_result.path[0] = 0;
                state.card_region_latitude = peer.position.latitude_i;
                state.card_region_longitude = peer.position.longitude_i;
                state.card_region_coordinate_valid = true;
            }
            if (!state.card_region_request)
                state.card_region_request = map_search::submitRegion(peer.position.latitude_i * 1e-7,
                                                                     peer.position.longitude_i * 1e-7);
            if (state.card_region_request && !map_search::pollRegion(state.card_region_request, state.card_region_result))
            {
                state.card_region_request = 0;
                state.card_region_result.path[0] = 0;
            }
            if (state.card_region_result.path[0])
            {
                const std::string region = std::string("~ ") + state.card_region_result.path;
                // Preserve the animation across periodic refreshes of the same path.
                if (std::strcmp(lv_label_get_text(state.card_region), region.c_str()) != 0)
                    ::ui::i18n::set_content_label_text_raw(state.card_region, region.c_str());
            }
            else ::ui::i18n::set_label_text(state.card_region, state.card_region_request &&
                                                                       state.card_region_result.status == map_search::Status::Searching
                                                                   ? "Location loading..."
                                                                   : "Region unknown");
        }
        else
        {
            state.card_region_coordinate_valid = false;
            state.card_region_request = 0;
            state.card_region_result.path[0] = 0;
            ::ui::i18n::set_label_text(state.card_region, "Position unknown");
        }
        const auto own = ::platform::ui::gps::get_data();
        const double latitude = peer.position.latitude_i * 1e-7;
        const double longitude = peer.position.longitude_i * 1e-7;
        if (peer.position.valid && std::abs(latitude) <= 90.0 && std::abs(longitude) <= 180.0 &&
            own.valid && std::isfinite(own.lat) && std::isfinite(own.lng) &&
            std::abs(own.lat) <= 90.0 && std::abs(own.lng) <= 180.0)
        {
            constexpr double rad = 3.14159265358979323846 / 180.0;
            const double dlat = (latitude - own.lat) * rad;
            const double dlon = (longitude - own.lng) * rad;
            const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                             std::cos(latitude * rad) * std::cos(own.lat * rad) *
                                 std::sin(dlon / 2) * std::sin(dlon / 2);
            const double meters = 6371000.0 * 2 * std::asin(std::sqrt(std::clamp(a, 0.0, 1.0)));
            const auto distance = meters < 1000 ? ::ui::i18n::format("Distance %.0f m", meters) : ::ui::i18n::format("Distance %.1f km", meters / 1000);
            std::snprintf(text, sizeof(text), "%s", distance.c_str());
            lv_label_set_text(state.card_distance, text);
        }
        else ::ui::i18n::set_label_text(state.card_distance, own.valid ? "Distance unknown" : "My position unknown");
        const auto age = format_time_status(peer.last_seen);
        if (is_reticulum_node(peer))
            std::snprintf(text, sizeof(text), "%s | %s", age.c_str(), ::ui::i18n::tr(peer.key_manually_verified ? "Verified" : "Unverified"));
        else if (peer.position.valid && peer.position.timestamp && sys::epoch_seconds_now() > peer.position.timestamp &&
                 sys::epoch_seconds_now() - peer.position.timestamp > 3600)
            std::snprintf(text, sizeof(text), "%s | %s", age.c_str(), ::ui::i18n::tr("Old position"));
        else if (peer.has_snr && std::isfinite(peer.snr))
            std::snprintf(text, sizeof(text), "%s | SNR %.1f dB", age.c_str(), peer.snr);
        else std::snprintf(text, sizeof(text), "%s", age.c_str());
        lv_label_set_text(state.card_status, text);
    }
    std::snprintf(text, sizeof(text), "%u / %u", state.card_entries.empty() ? 0U : static_cast<unsigned>(state.card_index + 1),
                  static_cast<unsigned>(state.card_entries.size()));
    lv_label_set_text(state.card_counter, text);
    if (state.card_index == 0) lv_obj_add_state(state.prev_btn, LV_STATE_DISABLED);
    else lv_obj_clear_state(state.prev_btn, LV_STATE_DISABLED);
    if (state.card_index + 1 >= state.card_entries.size()) lv_obj_add_state(state.next_btn, LV_STATE_DISABLED);
    else lv_obj_clear_state(state.next_btn, LV_STATE_DISABLED);
    refresh_filter_checked_state();
    state.rendered_data_revision = state.contacts_data_revision;
    if (created) contacts_input_on_ui_refreshed();
}

static void refresh_filter_availability()
{
    const bool team_available = is_team_available() && chat_support::supports_team_chat();
    const bool reticulum_profile = uses_reticulum_filter_profile();
    const bool meshcore_mode = uses_meshcore_filter_profile();
    if (g_contacts_state.team_btn)
    {
        if (team_available && !reticulum_profile)
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
    if ((!team_available || reticulum_profile) && g_contacts_state.current_mode == ContactsMode::Team)
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
    if (!reticulum_profile && g_contacts_state.current_mode == ContactsMode::Groups)
    {
        g_contacts_state.current_mode = ContactsMode::Contacts;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
    }
    if (reticulum_profile && g_contacts_state.current_mode == ContactsMode::Broadcast)
    {
        g_contacts_state.current_mode = ContactsMode::Contacts;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
    }
    if (g_contacts_state.current_mode == ContactsMode::Public)
    {
        g_contacts_state.current_mode = ContactsMode::Contacts;
        g_contacts_state.current_page = 0;
        g_contacts_state.selected_index = -1;
    }
}

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
    // Filters belong to the page, including when its content is a node card.
    refresh_filter_availability();
    if (uses_node_card())
    {
        refresh_node_card_ui();
        s_refreshing_ui = false;
        return;
    }
    if (g_contacts_state.node_card)
    {
        clear_footer_actions();
        lv_obj_clean(g_contacts_state.list_panel);
        g_contacts_state.node_card = nullptr;
        g_contacts_state.sub_container = nullptr;
        g_contacts_state.prev_btn = g_contacts_state.next_btn = g_contacts_state.back_btn = nullptr;
        g_contacts_state.list_items.clear();
        g_contacts_state.card_context_valid = false;
        refresh_contacts_data();
    }

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

    const bool same_render_context =
        g_contacts_state.rendered_mode_valid &&
        g_contacts_state.rendered_mode == g_contacts_state.current_mode &&
        std::strcmp(g_contacts_state.rendered_search_query,
                    g_contacts_state.search_query) == 0;
    const int saved_scroll_y =
        same_render_context && g_contacts_state.sub_container &&
                lv_obj_is_valid(g_contacts_state.sub_container)
            ? lv_obj_get_scroll_y(g_contacts_state.sub_container)
            : 0;

    lv_obj_clear_flag(g_contacts_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
    if (g_contacts_state.sub_container)
    {
        lv_obj_clear_flag(g_contacts_state.sub_container, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Ensure list containers exist (structure handled in layout)
    contacts::ui::layout::ensure_list_subcontainers();

    if (g_contacts_state.empty_label != nullptr)
    {
        if (lv_obj_is_valid(g_contacts_state.empty_label))
        {
            lv_obj_del(g_contacts_state.empty_label);
        }
        g_contacts_state.empty_label = nullptr;
    }

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
    std::vector<chat::contacts::PeerDirectoryItem> broadcast_list;
    std::vector<chat::contacts::PeerDirectoryItem> team_list;
    std::vector<chat::contacts::PeerDirectoryItem> discover_list;
    const std::vector<chat::contacts::PeerDirectoryItem>* current_list = nullptr;
    if (g_contacts_state.current_mode == ContactsMode::Groups)
    {
        current_list = &g_contacts_state.reticulum_group_list;
    }
    else if (g_contacts_state.current_mode == ContactsMode::Team)
    {
        contacts::ui::ContactsTeamSnapshot team_snapshot;
        (void)load_contacts_team_snapshot(team_snapshot);
        chat::contacts::PeerDirectoryItem team_node{};
        team_node.node_id = 0;
        team_node.last_seen = 0;
        team_node.snr = 0.0f;
        team_node.is_contact = false;
        team_node.protocol = chat::contacts::NodeProtocolType::Unknown;
        team_node.display_name =
            contacts::ui::contactsTeamDisplayName(team_snapshot,
                                                  ::ui::i18n::tr("Team"));
        team_list.push_back(team_node);
        current_list = &team_list;
    }
    else if (g_contacts_state.current_mode == ContactsMode::Discover)
    {
        for (size_t i = 0; i < (sizeof(kDiscoveryActionSpecs) / sizeof(kDiscoveryActionSpecs[0])); ++i)
        {
            chat::contacts::PeerDirectoryItem item{};
            item.node_id = static_cast<uint32_t>(i + 1);
            item.display_name = ::ui::i18n::tr(kDiscoveryActionSpecs[i].label);
            item.protocol = chat::contacts::NodeProtocolType::MeshCore;
            discover_list.push_back(item);
        }
        current_list = &discover_list;
    }
    else
    {
        const size_t target_count = get_broadcast_target_count();
        for (size_t i = 0; i < target_count; ++i)
        {
            BroadcastTargetSpec spec{};
            if (!get_broadcast_target_spec(static_cast<int>(i), &spec))
            {
                continue;
            }
            chat::contacts::PeerDirectoryItem target{};
            target.display_name = format_broadcast_target_label(spec);
            target.protocol = (spec.protocol == chat::MeshProtocol::MeshCore)
                                  ? chat::contacts::NodeProtocolType::MeshCore
                                  : (chat::infra::isReticulumMeshProtocol(spec.protocol)
                                         ? chat::contacts::NodeProtocolType::Reticulum
                                         : chat::contacts::NodeProtocolType::Meshtastic);
            target.channel = spec.channel_index;
            broadcast_list.push_back(target);
        }
        current_list = &broadcast_list;
    }

    if (current_list && use_search_display_list_for_mode(g_contacts_state.current_mode))
    {
        build_display_list(*current_list);
        current_list = &g_contacts_state.display_list;
    }
    else
    {
        g_contacts_state.display_list.clear();
    }

    const bool show_reticulum_group_add_item =
        g_contacts_state.current_mode == ContactsMode::Groups;
    g_contacts_state.total_items = current_list->size();
    if (show_reticulum_group_add_item)
    {
        g_contacts_state.total_items += 1;
    }

    const bool use_scroll_list =
        (g_contacts_state.current_mode == ContactsMode::Contacts) ||
        (g_contacts_state.current_mode == ContactsMode::Nearby) ||
        (g_contacts_state.current_mode == ContactsMode::Groups) ||
        (g_contacts_state.current_mode == ContactsMode::Ignored) ||
        (g_contacts_state.current_mode == ContactsMode::Broadcast) ||
        (g_contacts_state.current_mode == ContactsMode::Discover);
    int target_scroll_y = same_render_context ? saved_scroll_y : 0;
    if (target_scroll_y < 0)
    {
        target_scroll_y = 0;
    }

    if (g_contacts_state.selected_index >= static_cast<int>(g_contacts_state.total_items))
    {
        g_contacts_state.selected_index = -1;
    }

    // Pagination calc (unchanged)
    int total_pages = use_scroll_list ? 1 : (static_cast<int>(g_contacts_state.total_items) + kItemsPerPage - 1) / kItemsPerPage;
    if (total_pages == 0) total_pages = 1;

    if (g_contacts_state.current_page >= total_pages)
    {
        g_contacts_state.current_page = total_pages - 1;
    }
    if (g_contacts_state.current_page < 0)
    {
        g_contacts_state.current_page = 0;
    }

    int start_idx = use_scroll_list ? 0 : (g_contacts_state.current_page * kItemsPerPage);
    int end_idx = use_scroll_list ? static_cast<int>(current_list->size()) : (start_idx + kItemsPerPage);
    if (end_idx > static_cast<int>(current_list->size()))
    {
        end_idx = static_cast<int>(current_list->size());
    }
    if (show_reticulum_group_add_item)
    {
        chat::contacts::PeerDirectoryItem add_node{};
        add_node.protocol = chat::contacts::NodeProtocolType::Reticulum;
        std::snprintf(add_node.long_name, sizeof(add_node.long_name), "%s", "Add Group");
        add_node.display_name = add_node.long_name;
        std::snprintf(add_node.short_name, sizeof(add_node.short_name), "%s", "+");
        const char* add_status =
            g_contacts_state.reticulum_group_storage_ready
                ? "Configure"
                : (g_contacts_state.reticulum_group_storage_message[0] != '\0'
                       ? g_contacts_state.reticulum_group_storage_message
                       : "SD card required");
        lv_obj_t* add_item = contacts::ui::layout::create_list_item(
            g_contacts_state.sub_container,
            add_node,
            g_contacts_state.current_mode,
            add_status);
        lv_obj_set_user_data(add_item, reinterpret_cast<void*>(kAddReticulumGroupUserData));
        lv_obj_add_event_cb(add_item, on_list_item_clicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(add_item, on_list_item_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(add_item);
    }

    const bool searchable_mode =
        is_searchable_contacts_mode(g_contacts_state.current_mode);
    if (current_list->empty() && search_active() && searchable_mode)
    {
        const char* label_text = "No matches";
        g_contacts_state.empty_label = lv_label_create(g_contacts_state.sub_container);
        ::ui::i18n::set_label_text(g_contacts_state.empty_label, label_text);
        contacts::ui::style::apply_label_muted(g_contacts_state.empty_label);
        lv_obj_set_width(g_contacts_state.empty_label, LV_PCT(100));
        lv_obj_set_style_pad_all(g_contacts_state.empty_label, 8, LV_PART_MAIN);
        lv_obj_set_style_text_align(g_contacts_state.empty_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }

    // Create list items for current page (structure in layout; status string computed here)
    for (int i = start_idx; i < end_idx; ++i)
    {
        const auto& node = (*current_list)[i];

        std::string status_text;
        if (g_contacts_state.current_mode == ContactsMode::Groups)
        {
            status_text = chat_support::supports_reticulum_destination_text()
                              ? ::ui::i18n::tr("Ready")
                              : ::ui::i18n::tr("Unavailable");
        }
        else if (g_contacts_state.current_mode == ContactsMode::Team)
        {
            status_text = ::ui::i18n::tr("Team");
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
                status_text = ::ui::i18n::tr("Action");
            }
        }
        else
        {
            BroadcastTargetSpec spec{};
            if (get_broadcast_target_spec(i, &spec))
            {
                status_text = format_broadcast_target_status(spec);
            }
            else
            {
                status_text = ::ui::i18n::tr("Channel");
            }
        }

        lv_obj_t* item = contacts::ui::layout::create_list_item(
            g_contacts_state.sub_container,
            node,
            g_contacts_state.current_mode,
            status_text.c_str());

        // Store the global index on the item so click handlers know which entry was selected.
        lv_obj_set_user_data(item, (void*)(intptr_t)i);

        // Clicking a list row opens the action menu.
        lv_obj_add_event_cb(item, on_list_item_clicked, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(item, on_list_item_focused, LV_EVENT_FOCUSED, nullptr);
        bind_page_shortcuts(item);
    }

    // Create bottom buttons (create once; width follows label text)
    if (g_contacts_state.next_btn == nullptr)
    {
        g_contacts_state.next_btn = create_bottom_bar_button(
            g_contacts_state.bottom_container,
            "Next",
            kColorAmber,
            on_next_clicked);
        bind_page_shortcuts(g_contacts_state.next_btn);
    }

    if (g_contacts_state.prev_btn == nullptr)
    {
        g_contacts_state.prev_btn = create_bottom_bar_button(
            g_contacts_state.bottom_container,
            "Prev",
            kColorPanelBg,
            on_prev_clicked);
        bind_page_shortcuts(g_contacts_state.prev_btn);
    }

    if (show_second_column_back() && g_contacts_state.back_btn == nullptr)
    {
        g_contacts_state.back_btn = create_bottom_bar_button(
            g_contacts_state.bottom_container,
            "Back",
            kColorAmber,
            on_back_clicked);
        bind_page_shortcuts(g_contacts_state.back_btn);
    }

    if (g_contacts_state.back_btn)
    {
        if (show_second_column_back())
        {
            lv_obj_clear_flag(g_contacts_state.back_btn, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_contacts_state.back_btn, LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (auto* button : {g_contacts_state.prev_btn, g_contacts_state.next_btn})
    {
        if (use_scroll_list) lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
    }
    arrange_footer_actions();

    // Enable/disable buttons based on pagination (unchanged from original)
    if (!use_scroll_list && total_pages > 1)
    {
        lv_obj_clear_state(g_contacts_state.prev_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(g_contacts_state.next_btn, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_add_state(g_contacts_state.prev_btn, LV_STATE_DISABLED);
        lv_obj_add_state(g_contacts_state.next_btn, LV_STATE_DISABLED);
    }
    if (g_contacts_state.back_btn)
    {
        lv_obj_clear_state(g_contacts_state.back_btn, LV_STATE_DISABLED);
    }

    // Update filter highlights (visual-only, using CHECKED state).
    refresh_filter_checked_state();

    if (g_contacts_state.list_panel)
    {
        lv_obj_scroll_to_y(g_contacts_state.list_panel, 0, LV_ANIM_OFF);
        lv_obj_invalidate(g_contacts_state.list_panel);
        if (use_scroll_list)
        {
            lv_obj_clear_flag(g_contacts_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(g_contacts_state.list_panel, LV_SCROLLBAR_MODE_OFF);
        }
        else
        {
            lv_obj_add_flag(g_contacts_state.list_panel, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_scrollbar_mode(g_contacts_state.list_panel, LV_SCROLLBAR_MODE_AUTO);
        }
    }
    if (g_contacts_state.sub_container)
    {
        lv_obj_add_flag(g_contacts_state.sub_container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(g_contacts_state.sub_container, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(g_contacts_state.sub_container,
                                  use_scroll_list ? LV_SCROLLBAR_MODE_AUTO : LV_SCROLLBAR_MODE_OFF);
        if (use_scroll_list)
        {
            lv_obj_update_layout(g_contacts_state.sub_container);
            lv_obj_scroll_to_y(g_contacts_state.sub_container, target_scroll_y, LV_ANIM_OFF);
        }
    }
    g_contacts_state.rendered_mode = g_contacts_state.current_mode;
    g_contacts_state.rendered_mode_valid = true;
    g_contacts_state.rendered_data_revision = g_contacts_state.contacts_data_revision;
    std::snprintf(g_contacts_state.rendered_search_query,
                  sizeof(g_contacts_state.rendered_search_query),
                  "%s",
                  g_contacts_state.search_query);
    s_refreshing_ui = false;
    contacts_input_on_ui_refreshed();
}

// ---------------- Modal cleanup (public API) ----------------

void cleanup_modals()
{
    ::ui::components::floating_search_box::close(g_contacts_state.search_box);
    ::ui::components::floating_search_box::close(g_contacts_state.lxmf_address_box);
    if (g_contacts_state.empty_label != nullptr)
    {
        if (lv_obj_is_valid(g_contacts_state.empty_label))
        {
            lv_obj_del(g_contacts_state.empty_label);
        }
        g_contacts_state.empty_label = nullptr;
    }
    if (g_contacts_state.add_edit_modal != nullptr)
    {
        lv_obj_del(g_contacts_state.add_edit_modal);
        g_contacts_state.add_edit_modal = nullptr;
    }
    g_contacts_state.add_edit_textarea = nullptr;
    g_contacts_state.add_edit_error_label = nullptr;
    if (g_contacts_state.reticulum_group_modal != nullptr)
    {
        lv_obj_del(g_contacts_state.reticulum_group_modal);
        g_contacts_state.reticulum_group_modal = nullptr;
    }
    g_contacts_state.reticulum_group_name_textarea = nullptr;
    g_contacts_state.reticulum_group_destination_textarea = nullptr;
    g_contacts_state.reticulum_group_error_label = nullptr;
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
    ::ui::components::shortcut_help_modal::close(s_help_modal);
    if (g_contacts_state.discover_scan_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.discover_scan_timer);
        g_contacts_state.discover_scan_timer = nullptr;
    }
    if (g_contacts_state.node_info_root != nullptr)
    {
        if (g_contacts_state.reticulum_node_info_active)
        {
            if (lv_obj_is_valid(g_contacts_state.node_info_root))
            {
                lv_obj_del(g_contacts_state.node_info_root);
            }
            s_reticulum_node_info_top_bar = ::ui::widgets::TopBar{};
            g_contacts_state.reticulum_node_info_active = false;
        }
        else
        {
            node_info::ui::destroy();
        }
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
    reset_conversation_runtime_state();
    reset_contacts_team_chat_runtime();
}
