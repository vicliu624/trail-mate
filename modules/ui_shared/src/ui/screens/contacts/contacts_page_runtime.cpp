#include "ui/screens/contacts/contacts_page_runtime.h"
#include "ui/components/bottom_bar.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "ui/app_runtime.h"
#include "ui/screens/chat/chat_conversation_components.h"
#include "ui/screens/contacts/contacts_page_components.h"
#include "ui/screens/contacts/contacts_page_input.h"
#include "ui/screens/contacts/contacts_page_layout.h"
#include "ui/screens/contacts/contacts_state.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>

#define CONTACTS_DEBUG 0
#if CONTACTS_DEBUG
#define CONTACTS_LOG(...) std::printf(__VA_ARGS__)
#else
#define CONTACTS_LOG(...)
#endif

using namespace contacts::ui;

namespace
{

using contacts::ui::shell::Host;

constexpr uint32_t kContactsRefreshIntervalMs = 2000;

const Host* s_host = nullptr;

bool contacts_initial_hydration_pending()
{
    return app::hasAppFacade() &&
           app::runtimeFacade().isInitialStorageHydrationPending();
}

void destroy_contacts_page_runtime()
{
    if (g_contacts_state.conversation_screen)
    {
        delete g_contacts_state.conversation_screen;
        g_contacts_state.conversation_screen = nullptr;
    }
    if (g_contacts_state.conversation_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.conversation_timer);
        g_contacts_state.conversation_timer = nullptr;
    }
    if (g_contacts_state.discover_scan_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.discover_scan_timer);
        g_contacts_state.discover_scan_timer = nullptr;
    }
    if (g_contacts_state.refresh_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.refresh_timer);
        g_contacts_state.refresh_timer = nullptr;
    }

    cleanup_modals();
    cleanup_contacts_input();

    if (g_contacts_state.root != nullptr)
    {
        lv_obj_del(g_contacts_state.root);
        g_contacts_state.root = nullptr;
    }

    // Clear every handle after the object tree is gone. Re-entering the page
    // must never let refresh_ui() observe children from the previous tree.
    g_contacts_state.~ContactsPageState();
    new (&g_contacts_state) ContactsPageState{};
}

static uint32_t hash_step(uint32_t hash, uint8_t byte)
{
    return (hash ^ byte) * 16777619U;
}

static uint32_t hash_bytes(uint32_t hash, const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return hash_step(hash, 0);
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        hash = hash_step(hash, data[i]);
    }
    return hash;
}

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
    hash = hash_step(hash, static_cast<uint8_t>(value & 0xFFU));
    hash = hash_step(hash, static_cast<uint8_t>((value >> 8) & 0xFFU));
    hash = hash_step(hash, static_cast<uint8_t>((value >> 16) & 0xFFU));
    hash = hash_step(hash, static_cast<uint8_t>((value >> 24) & 0xFFU));
    return hash;
}

static uint32_t hash_text(uint32_t hash, const char* text)
{
    if (!text)
    {
        return hash_step(hash, 0);
    }
    while (*text)
    {
        hash = hash_step(hash, static_cast<uint8_t>(*text));
        ++text;
    }
    return hash_step(hash, 0);
}

static uint32_t hash_string(uint32_t hash, const std::string& text)
{
    return hash_text(hash, text.c_str());
}

static uint32_t hash_reticulum_identity(uint32_t hash,
                                        const chat::ReticulumPeerIdentity& identity)
{
    hash = hash_bytes(hash,
                      identity.destination_hash,
                      sizeof(identity.destination_hash));
    hash = hash_bytes(hash, identity.identity_hash, sizeof(identity.identity_hash));
    hash = hash_step(hash, identity.valid ? 1U : 0U);
    return hash;
}

static uint32_t hash_node(uint32_t hash, const chat::contacts::PeerDirectoryItem& node)
{
    hash = hash_u32(hash, node.node_id);
    hash = hash_text(hash, node.short_name);
    hash = hash_text(hash, node.long_name);
    hash = hash_string(hash, node.display_name);
    hash = hash_step(hash, static_cast<uint8_t>(node.protocol));
    hash = hash_step(hash, static_cast<uint8_t>(node.role));
    hash = hash_step(hash, node.channel);
    hash = hash_step(hash, node.hops_away);
    hash = hash_step(hash, node.is_contact ? 1U : 0U);
    hash = hash_step(hash, node.is_ignored ? 1U : 0U);
    hash = hash_step(hash, node.has_public_key ? 1U : 0U);
    hash = hash_step(hash, node.key_manually_verified ? 1U : 0U);
    return hash_reticulum_identity(hash, node.reticulum_identity);
}

static uint32_t hash_node_list(uint32_t hash,
                               const std::vector<chat::contacts::PeerDirectoryItem>& nodes)
{
    hash = hash_u32(hash, static_cast<uint32_t>(nodes.size()));
    for (const auto& node : nodes)
    {
        hash = hash_node(hash, node);
    }
    return hash;
}

static uint32_t compute_contacts_data_signature()
{
    uint32_t hash = 2166136261U;
    hash = hash_node_list(hash, g_contacts_state.reticulum_group_list);
    hash = hash_step(hash, g_contacts_state.reticulum_group_storage_supported ? 1U : 0U);
    hash = hash_step(hash, g_contacts_state.reticulum_group_storage_ready ? 1U : 0U);
    hash = hash_step(hash, g_contacts_state.reticulum_group_storage_loaded ? 1U : 0U);
    hash = hash_text(hash, g_contacts_state.reticulum_group_storage_message);
    hash = hash_text(hash, g_contacts_state.reticulum_group_storage_detail);
    return hash;
}

static void mark_contacts_data_refreshed()
{
    const uint32_t signature = compute_contacts_data_signature();
    if (!g_contacts_state.contacts_data_signature_valid ||
        signature != g_contacts_state.contacts_data_signature)
    {
        g_contacts_state.contacts_data_signature = signature;
        ++g_contacts_state.contacts_data_revision;
        if (g_contacts_state.contacts_data_revision == 0)
        {
            g_contacts_state.contacts_data_revision = 1;
        }
        g_contacts_state.contacts_data_signature_valid = true;
    }
}

void format_reticulum_hash_prefix(const chat::ReticulumPeerIdentity& identity,
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

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

void contacts_top_bar_back(void*)
{
    if (g_contacts_state.exiting)
    {
        return;
    }
    g_contacts_state.exiting = true;
    if (g_contacts_state.refresh_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.refresh_timer);
        g_contacts_state.refresh_timer = nullptr;
    }
    if (g_contacts_state.conversation_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.conversation_timer);
        g_contacts_state.conversation_timer = nullptr;
    }
    if (g_contacts_state.discover_scan_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.discover_scan_timer);
        g_contacts_state.discover_scan_timer = nullptr;
    }
    request_exit();
}

void contacts_refresh_timer_cb(lv_timer_t*)
{
    const auto& state = g_contacts_state;
    if (state.exiting || !state.root || state.node_info_root || state.action_menu_modal ||
        state.add_edit_modal || state.del_confirm_modal || state.search_box.overlay) return;
    const auto revision = state.contacts_data_revision;
    refresh_contacts_data();
    g_contacts_state.contacts_data_hydration_pending = contacts_initial_hydration_pending();
    if (uses_node_card() || revision != state.contacts_data_revision) refresh_ui();
}

void refresh_reticulum_groups_data()
{
    g_contacts_state.reticulum_group_list.clear();
    const app::AppConfig& config = app::configFacade().readConfig();
    const chat::MeshProtocol active_protocol =
        chat::infra::normalizeMeshProtocol(config.mesh_protocol);
    if (!chat::infra::isReticulumMeshProtocol(active_protocol))
    {
        g_contacts_state.reticulum_group_storage_supported = false;
        g_contacts_state.reticulum_group_storage_ready = false;
        g_contacts_state.reticulum_group_storage_loaded = false;
        g_contacts_state.reticulum_group_storage_message[0] = '\0';
        g_contacts_state.reticulum_group_storage_detail[0] = '\0';
        return;
    }

    g_contacts_state.reticulum_group_storage_supported = true;
    g_contacts_state.reticulum_group_storage_ready = true;
    g_contacts_state.reticulum_group_storage_loaded = true;
    g_contacts_state.reticulum_group_storage_message[0] = '\0';
    g_contacts_state.reticulum_group_storage_detail[0] = '\0';
    const auto& groups = config.reticulumConfig().reticulum_groups;

    for (std::size_t index = 0; index < chat::kReticulumGroupDestinationMaxCount; ++index)
    {
        const chat::ReticulumGroupDestinationConfig& group = groups[index];
        if (!group.enabled ||
            !chat::hasReticulumDestinationIdentity(group.identity))
        {
            continue;
        }

        chat::contacts::PeerDirectoryItem item{};
        item.node_id = 0;
        item.protocol = chat::contacts::NodeProtocolType::Reticulum;
        item.role = chat::contacts::NodeRoleType::Client;
        item.channel = static_cast<uint8_t>(index);
        item.reticulum_identity = group.identity;
        const char* name = group.name[0] != '\0' ? group.name : nullptr;
        char fallback[chat::kReticulumGroupNameMaxLen] = {};
        if (!name)
        {
            std::snprintf(fallback,
                          sizeof(fallback),
                          "Group %u",
                          static_cast<unsigned>(index + 1));
            name = fallback;
        }
        std::strncpy(item.long_name, name, sizeof(item.long_name) - 1);
        item.long_name[sizeof(item.long_name) - 1] = '\0';
        item.display_name = item.long_name;
        std::snprintf(item.short_name,
                      sizeof(item.short_name),
                      "RT%u",
                      static_cast<unsigned>(index + 1));
        g_contacts_state.reticulum_group_list.push_back(item);

        char dest_hash[12] = {};
        format_reticulum_hash_prefix(group.identity, dest_hash, sizeof(dest_hash));
        std::printf("[Contacts][RTGroup] configured index=%u name=%s dest=%s\n",
                    static_cast<unsigned>(index),
                    name,
                    dest_hash);
    }
}

void refresh_contacts_data_impl_internal()
{
    if (uses_node_card()) refresh_node_card_data();
    else
    {
        refresh_reticulum_groups_data();
        mark_contacts_data_refreshed();
    }
}

} // namespace

namespace contacts::ui
{
void hydrate_node_card()
{
    auto& state = g_contacts_state;
    state.card_peer_valid = false;
    if (!state.contact_service || state.card_index >= state.card_entries.size()) return;
    state.card_peer_valid = state.contact_service->readPeer(state.card_entries[state.card_index], state.card_peer).succeeded();
    state.selected_index = state.card_peer_valid ? 0 : -1;
}

void refresh_node_card_data()
{
    auto& state = g_contacts_state;
    auto& service = app::messagingFacade().getContactService();
    service.setActiveProtocol(app::configFacade().readConfig().mesh_protocol);
    const bool same_context = state.card_context_valid && state.card_mode == state.current_mode &&
                              std::strcmp(state.card_query, state.search_query) == 0;
    chat::contacts::PeerBrowseEntry selected{};
    const bool keep_selection = same_context && state.card_index < state.card_entries.size();
    if (keep_selection) selected = state.card_entries[state.card_index];
    std::vector<chat::contacts::PeerBrowseEntry> next;
    const auto view = state.current_mode == ContactsMode::Contacts ? chat::MeshPeerDirectoryView::Contacts : state.current_mode == ContactsMode::Ignored ? chat::MeshPeerDirectoryView::Ignored
                                                                                                                                                         : chat::MeshPeerDirectoryView::Nearby;
    const auto status = service.browsePeers(view, state.search_query, next);
    if (!status.succeeded()) return; // A busy directory must not erase the current card.
    state.card_entries.swap(next);
    if (!same_context) state.card_index = 0;
    if (keep_selection)
    {
        for (size_t i = 0; i < state.card_entries.size(); ++i)
            if (state.card_entries[i].samePeer(selected))
            {
                state.card_index = i;
                break;
            }
    }
    if (state.card_index >= state.card_entries.size()) state.card_index = 0;
    state.card_mode = state.current_mode;
    state.card_context_valid = true;
    std::snprintf(state.card_query, sizeof(state.card_query), "%.*s",
                  static_cast<int>(sizeof(state.card_query) - 1), state.search_query);
    // Release full projections left by another mode; only one peer is hydrated.
    state.total_items = state.card_entries.size();
    hydrate_node_card();
    ++state.contacts_data_revision;
}
} // namespace contacts::ui

void refresh_contacts_data_impl()
{
    refresh_contacts_data_impl_internal();
}

namespace contacts::ui::runtime
{

bool is_available()
{
    return app::hasAppFacade();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    if (!is_available())
    {
        return;
    }

    s_host = host;

    CONTACTS_LOG("[Contacts] Entering Contacts page\n");

    if (g_contacts_state.root != nullptr)
    {
        destroy_contacts_page_runtime();
    }

    g_contacts_state.exiting = false;
    g_contacts_state.contact_service = &app::messagingFacade().getContactService();
    g_contacts_state.chat_service = &app::messagingFacade().getChatService();

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    g_contacts_state.root = contacts::ui::layout::create_root(parent);
    contacts::ui::layout::create_header(g_contacts_state.root, contacts_top_bar_back, nullptr);

    lv_obj_t* content = contacts::ui::layout::create_content(g_contacts_state.root);
    g_contacts_state.page = content;
    ui_update_top_bar_battery(g_contacts_state.top_bar);

    create_filter_panel(content);
    contacts::ui::layout::create_list_panel(content);
    g_contacts_state.bottom_container = ::ui::components::bottom_bar::create(g_contacts_state.root);

    set_default_group(prev_group);

    g_contacts_state.current_mode = ContactsMode::Contacts;
    g_contacts_state.last_action_mode = ContactsMode::Contacts;
    g_contacts_state.current_page = 0;
    g_contacts_state.selected_index = -1;
    g_contacts_state.rendered_mode_valid = false;
    g_contacts_state.rendered_search_query[0] = '\0';
    g_contacts_state.contacts_data_signature = 0;
    g_contacts_state.contacts_data_revision = 0;
    g_contacts_state.rendered_data_revision = 0;
    g_contacts_state.contacts_data_signature_valid = false;
    g_contacts_state.contacts_data_hydration_pending = false;
    g_contacts_state.focused_filter_mode = ContactsMode::Contacts;
    g_contacts_state.focused_filter_mode_valid = false;

    init_contacts_input();
    refresh_contacts_data();
    g_contacts_state.contacts_data_hydration_pending = contacts_initial_hydration_pending();
    refresh_ui();
    g_contacts_state.refresh_timer =
        lv_timer_create(contacts_refresh_timer_cb, kContactsRefreshIntervalMs, nullptr);
    if (g_contacts_state.refresh_timer)
    {
        std::printf("[Contacts] refresh timer started interval_ms=%u\n",
                    static_cast<unsigned>(kContactsRefreshIntervalMs));
    }

    g_contacts_state.initialized = true;
    CONTACTS_LOG("[Contacts] Contacts page initialized\n");
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    if (!is_available())
    {
        s_host = nullptr;
        return;
    }

    CONTACTS_LOG("[Contacts] Exiting Contacts page\n");

    destroy_contacts_page_runtime();
    CONTACTS_LOG("[Contacts] Contacts page cleaned up\n");
    s_host = nullptr;
}

} // namespace contacts::ui::runtime
