/**
 * @file team_page_components.cpp
 * @brief Team page components
 */

#include "team_page_components.h"
#include "../../../app/app_context.h"
#include "../../../sys/event_bus.h"
#include "../../../team/protocol/team_mgmt.h"
#include "../../../team/protocol/team_track.h"
#include "../../../team/usecase/team_service.h"
#include "../../ui_common.h"
#include "../../widgets/system_notification.h"
#include "../gps/gps_state.h"
#include "../gps/gps_tracker_overlay.h"
#include "meshtastic/mesh.pb.h"
#include "pb_decode.h"
#include "team_page_input.h"
#include "team_page_layout.h"
#include "team_page_styles.h"
#include "team_state.h"
#include "team_ui_store.h"

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace team
{
namespace ui
{
namespace
{
constexpr int kActionBtnHeight = 28;
constexpr int kActionBtnWidth2 = 170;
constexpr int kActionBtnWidth3 = 140;
constexpr int kListItemHeight = 28;
constexpr uint8_t kKeyDistMaxRetries = 3;
constexpr uint32_t kKeyDistRetryIntervalSec = 5;
constexpr uint8_t kKeyRoleMember = 1;
bool s_state_loaded = false;

uint32_t now_secs();
bool is_team_ui_active();
bool is_team_chat_visible();
std::string resolve_node_name(uint32_t node_id);
bool is_pairing_active();
void sync_pairing_from_service();
void update_team_name_from_id(const TeamId& id);
void fill_status_members(team::proto::TeamStatus& status);
void apply_member_list_from_status(const team::proto::TeamStatus& status);

uint64_t team_id_to_u64(const TeamId& id)
{
    uint64_t value = 0;
    for (size_t i = 0; i < id.size(); ++i)
    {
        value |= (static_cast<uint64_t>(id[i]) << (8 * i));
    }
    return value;
}

void write_u32_le(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void write_u64_le(std::vector<uint8_t>& out, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
    {
        out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

bool append_key_event(TeamKeyEventType type, const std::vector<uint8_t>& payload)
{
    if (!g_team_state.has_team_id)
    {
        return false;
    }
    uint32_t seq = g_team_state.last_event_seq + 1;
    if (!team_ui_append_key_event(g_team_state.team_id,
                                  type,
                                  seq,
                                  now_secs(),
                                  payload.data(),
                                  payload.size()))
    {
        return false;
    }
    g_team_state.last_event_seq = seq;
    return true;
}

void notify_send_failed(const char* action, bool needs_keys)
{
    const char* msg = "Send failed";
    if (needs_keys)
    {
        msg = "Send failed (keys not ready)";
    }
    if (action && action[0])
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s: %s", action, msg);
        ::ui::SystemNotification::show(buf, 2000);
        return;
    }
    ::ui::SystemNotification::show(msg, 2000);
}

void notify_send_failed_detail(const char* action, team::TeamService::SendError err)
{
    const char* reason = "send failed";
    switch (err)
    {
    case team::TeamService::SendError::KeysNotReady:
        reason = "keys not ready";
        break;
    case team::TeamService::SendError::EncodeFail:
        reason = "encode failed";
        break;
    case team::TeamService::SendError::EncryptFail:
        reason = "encrypt failed";
        break;
    case team::TeamService::SendError::MeshSendFail:
        reason = "queue full";
        break;
    default:
        break;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %s", action ? action : "Send", reason);
    ::ui::SystemNotification::show(buf, 2000);
}

struct KeyDistPending
{
    uint32_t node_id = 0;
    uint32_t key_id = 0;
    uint8_t attempts = 0;
    uint32_t next_retry_s = 0;
};

std::vector<KeyDistPending> s_keydist_pending;

struct StatusBroadcastPending
{
    uint32_t next_send_s = 0;
    uint8_t remaining = 0;
};

std::vector<StatusBroadcastPending> s_status_pending;

void add_keydist_pending(uint32_t node_id, uint32_t key_id)
{
    for (auto& item : s_keydist_pending)
    {
        if (item.node_id == node_id && item.key_id == key_id)
        {
            return;
        }
    }
    KeyDistPending item;
    item.node_id = node_id;
    item.key_id = key_id;
    item.attempts = 0;
    item.next_retry_s = now_secs() + kKeyDistRetryIntervalSec;
    s_keydist_pending.push_back(item);
}

void mark_keydist_confirmed(uint32_t node_id, uint32_t key_id)
{
    s_keydist_pending.erase(
        std::remove_if(s_keydist_pending.begin(), s_keydist_pending.end(),
                       [&](const KeyDistPending& item)
                       {
                           return item.node_id == node_id && item.key_id == key_id;
                       }),
        s_keydist_pending.end());
}

void schedule_status_broadcast(uint8_t repeats, uint32_t delay_s)
{
    if (repeats == 0)
    {
        return;
    }
    StatusBroadcastPending item;
    item.next_send_s = now_secs() + delay_s;
    item.remaining = repeats;
    s_status_pending.push_back(item);
}

void process_keydist_retries()
{
    if (s_keydist_pending.empty())
    {
        return;
    }
    if (!g_team_state.has_team_psk || !g_team_state.has_team_id)
    {
        return;
    }
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    uint32_t now = now_secs();
    if (!controller)
    {
        return;
    }

    for (auto it = s_keydist_pending.begin(); it != s_keydist_pending.end();)
    {
        if (now < it->next_retry_s)
        {
            ++it;
            continue;
        }
        if (it->attempts >= kKeyDistMaxRetries)
        {
            notify_send_failed("KeyDist", false);
            it = s_keydist_pending.erase(it);
            continue;
        }

        team::proto::TeamKeyDist kd{};
        kd.team_id = g_team_state.team_id;
        kd.key_id = it->key_id;
        kd.channel_psk_len = static_cast<uint8_t>(g_team_state.team_psk.size());
        kd.channel_psk = g_team_state.team_psk;

        bool ok = controller->onKeyDistPlain(kd, chat::ChannelId::PRIMARY, it->node_id);
        if (!ok)
        {
            notify_send_failed_detail("KeyDist", controller->getLastSendError());
        }
        it->attempts += 1;
        it->next_retry_s = now + kKeyDistRetryIntervalSec;
        ++it;
    }
}

void process_status_broadcasts()
{
    if (s_status_pending.empty())
    {
        return;
    }
    if (!g_team_state.in_team || !g_team_state.has_team_id || !g_team_state.self_is_leader)
    {
        s_status_pending.clear();
        return;
    }
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (!controller)
    {
        return;
    }
    uint32_t now = now_secs();
    for (auto it = s_status_pending.begin(); it != s_status_pending.end();)
    {
        if (now < it->next_send_s)
        {
            ++it;
            continue;
        }
        team::proto::TeamStatus status{};
        status.key_id = g_team_state.security_round;
        fill_status_members(status);
        Serial.printf("[Team] status rebroadcast members=%u leader=%08lX\n",
                      static_cast<unsigned>(status.members.size()),
                      static_cast<unsigned long>(status.leader_id));
        if (!controller->onStatus(status, chat::ChannelId::PRIMARY, 0))
        {
            notify_send_failed("Status", true);
        }
        if (!controller->onStatusPlain(status, chat::ChannelId::PRIMARY, 0))
        {
            notify_send_failed("Status", false);
        }

        if (it->remaining > 0)
        {
            it->remaining -= 1;
        }
        if (it->remaining == 0)
        {
            it = s_status_pending.erase(it);
        }
        else
        {
            it->next_send_s = now + 2;
            ++it;
        }
    }
}

void update_top_bar_title(const char* title)
{
    if (!title)
    {
        return;
    }
    ::ui::widgets::top_bar_set_title(g_team_state.top_bar_widget, title);
}

void reset_team_ui_state()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller)
    {
        controller->resetUiState();
    }
}

void enter_kicked_out_state()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller)
    {
        controller->clearKeys();
    }
    team::TeamPairingService* pairing = app_ctx.getTeamPairing();
    if (pairing)
    {
        pairing->stop();
    }
    g_team_state.in_team = false;
    g_team_state.pending_join = false;
    g_team_state.pending_join_started_s = 0;
    g_team_state.kicked_out = true;
    g_team_state.self_is_leader = false;
    g_team_state.last_event_seq = 0;
    g_team_state.members.clear();
    g_team_state.has_team_id = false;
    g_team_state.team_name.clear();
    g_team_state.security_round = 0;
    g_team_state.has_team_psk = false;
    g_team_state.waiting_new_keys = false;
    s_keydist_pending.clear();
    s_status_pending.clear();
    g_team_state.page = TeamPage::KickedOut;
    g_team_state.nav_stack.clear();
}

void clear_focusables()
{
    g_team_state.focusables.clear();
    g_team_state.default_focus = nullptr;
}

void register_focus(lv_obj_t* obj, bool is_default = false)
{
    if (!obj)
    {
        return;
    }
    g_team_state.focusables.push_back(obj);
    if (is_default || !g_team_state.default_focus)
    {
        g_team_state.default_focus = obj;
    }
}

void clear_content()
{
    if (g_team_state.body)
    {
        lv_obj_clean(g_team_state.body);
    }
    if (g_team_state.actions)
    {
        lv_obj_clean(g_team_state.actions);
    }
    g_team_state.list_items.clear();
    for (auto& btn : g_team_state.action_btns)
    {
        btn = nullptr;
    }
    for (auto& label : g_team_state.action_labels)
    {
        label = nullptr;
    }
    g_team_state.detail_label = nullptr;
    clear_focusables();
}

void modal_prepare_group()
{
    if (!g_team_state.modal_group)
    {
        g_team_state.modal_group = lv_group_create();
    }
    lv_group_remove_all_objs(g_team_state.modal_group);
    g_team_state.prev_group = lv_group_get_default();
    set_default_group(g_team_state.modal_group);
}

void modal_restore_group()
{
    lv_group_t* restore = g_team_state.prev_group;
    if (!restore)
    {
        restore = g_team_state.group;
    }
    if (restore)
    {
        set_default_group(restore);
    }
    g_team_state.prev_group = nullptr;
}

lv_obj_t* create_modal_root(int width, int height)
{
    lv_obj_t* screen = lv_screen_active();
    lv_coord_t screen_w = lv_obj_get_width(screen);
    lv_coord_t screen_h = lv_obj_get_height(screen);

    lv_obj_t* bg = lv_obj_create(screen);
    lv_obj_set_size(bg, screen_w, screen_h);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x3A2A1A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, width, height);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_hex(0xFFF7E9), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0xD9B06A), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 8, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    return bg;
}

void close_leave_confirm_modal()
{
    if (g_team_state.leave_confirm_modal)
    {
        lv_obj_del(g_team_state.leave_confirm_modal);
        g_team_state.leave_confirm_modal = nullptr;
    }
    modal_restore_group();
}

uint32_t now_secs()
{
    return static_cast<uint32_t>(millis() / 1000);
}

int online_count()
{
    int count = 0;
    uint32_t now = now_secs();
    for (auto& m : g_team_state.members)
    {
        if (m.last_seen_s > 0 && (now - m.last_seen_s) <= 120)
        {
            m.online = true;
        }
        else
        {
            m.online = false;
        }
        if (m.online)
        {
            ++count;
        }
    }
    return count;
}

lv_obj_t* add_label(lv_obj_t* parent, const char* text, bool section = false, bool meta = false)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    if (section)
    {
        style::apply_section_label(label);
    }
    else if (meta)
    {
        style::apply_meta_label(label);
    }
    return label;
}

lv_obj_t* create_action_button(const char* text, int width, lv_event_cb_t cb)
{
    lv_obj_t* btn = lv_btn_create(g_team_state.actions);
    lv_obj_set_size(btn, width, kActionBtnHeight);
    style::apply_button_secondary(btn);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    return btn;
}

lv_obj_t* create_list_item(const char* left, const char* right)
{
    lv_obj_t* btn = lv_btn_create(g_team_state.body);
    lv_obj_set_size(btn, LV_PCT(100), kListItemHeight);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    style::apply_list_item(btn);

    lv_obj_t* left_label = lv_label_create(btn);
    lv_label_set_text(left_label, left);
    lv_obj_set_width(left_label, LV_PCT(70));
    lv_label_set_long_mode(left_label, LV_LABEL_LONG_CLIP);

    lv_obj_t* right_label = lv_label_create(btn);
    lv_label_set_text(right_label, right);
    lv_obj_set_width(right_label, LV_PCT(30));
    lv_label_set_long_mode(right_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(right_label, LV_TEXT_ALIGN_RIGHT, 0);

    g_team_state.list_items.push_back(btn);
    return btn;
}

std::string format_last_seen(uint32_t last_seen_s)
{
    if (last_seen_s == 0)
    {
        return "Last seen --";
    }
    uint32_t now = now_secs();
    if (now <= last_seen_s)
    {
        return "Online";
    }
    uint32_t age = now - last_seen_s;
    if (age <= 120)
    {
        return "Online";
    }
    if (age < 3600)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "Last seen %um ago", (unsigned)(age / 60));
        return std::string(buf);
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "Last seen %uh ago", (unsigned)(age / 3600));
    return std::string(buf);
}

std::string format_last_update(uint32_t last_update_s)
{
    if (last_update_s == 0)
    {
        return "Last update --";
    }
    uint32_t now = now_secs();
    if (now <= last_update_s)
    {
        return "Last update 0s ago";
    }
    uint32_t age = now - last_update_s;
    char buf[32];
    snprintf(buf, sizeof(buf), "Last update %us ago", (unsigned)age);
    return std::string(buf);
}

std::string format_team_name_from_id(const TeamId& id)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "TEAM-%02X%02X", id[0], id[1]);
    return std::string(buf);
}

std::string current_team_name()
{
    if (!g_team_state.team_name.empty())
    {
        return g_team_state.team_name;
    }
    if (g_team_state.has_team_id)
    {
        return format_team_name_from_id(g_team_state.team_id);
    }
    return "Unknown";
}

TeamId generate_team_id()
{
    TeamId id{};
    for (size_t i = 0; i < id.size(); ++i)
    {
        id[i] = static_cast<uint8_t>(random(0, 256));
    }
    return id;
}

std::string resolve_node_name(uint32_t node_id)
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    std::string name = app_ctx.getContactService().getContactName(node_id);
    if (!name.empty())
    {
        return name;
    }
    char fallback[16];
    snprintf(fallback, sizeof(fallback), "%08lX", static_cast<unsigned long>(node_id));
    return std::string(fallback);
}

int find_member_index(uint32_t node_id)
{
    for (size_t i = 0; i < g_team_state.members.size(); ++i)
    {
        if (g_team_state.members[i].node_id == node_id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void assign_member_color(TeamMemberUi& member)
{
    uint32_t node_id = member.node_id;
    if (node_id == 0)
    {
        node_id = app::AppContext::getInstance().getSelfNodeId();
    }
    member.color_index = team_color_index_from_node_id(node_id);
}

void ensure_member_colors()
{
    for (auto& m : g_team_state.members)
    {
        uint32_t node_id = m.node_id;
        if (node_id == 0)
        {
            node_id = app::AppContext::getInstance().getSelfNodeId();
        }
        m.color_index = team_color_index_from_node_id(node_id);
    }
}

void touch_member(uint32_t node_id, uint32_t last_seen_s)
{
    int idx = find_member_index(node_id);
    if (idx < 0)
    {
        TeamMemberUi info;
        info.node_id = node_id;
        info.name = resolve_node_name(node_id);
        info.last_seen_s = last_seen_s;
        assign_member_color(info);
        g_team_state.members.push_back(info);
        return;
    }
    g_team_state.members[idx].last_seen_s = last_seen_s;
    g_team_state.members[idx].name = resolve_node_name(node_id);
}

TeamUiSnapshot snapshot_from_state()
{
    TeamUiSnapshot snap;
    snap.in_team = g_team_state.in_team;
    snap.pending_join = g_team_state.pending_join;
    snap.pending_join_started_s = g_team_state.pending_join_started_s;
    snap.kicked_out = g_team_state.kicked_out;
    snap.self_is_leader = g_team_state.self_is_leader;
    snap.last_event_seq = g_team_state.last_event_seq;
    snap.team_chat_unread = g_team_state.team_chat_unread;
    snap.team_id = g_team_state.team_id;
    snap.has_team_id = g_team_state.has_team_id;
    snap.team_name = g_team_state.team_name;
    snap.security_round = g_team_state.security_round;
    snap.last_update_s = g_team_state.last_update_s;
    snap.team_psk = g_team_state.team_psk;
    snap.has_team_psk = g_team_state.has_team_psk;
    snap.members = g_team_state.members;
    return snap;
}

void apply_snapshot(const TeamUiSnapshot& snap)
{
    g_team_state.in_team = snap.in_team;
    g_team_state.pending_join = snap.pending_join;
    g_team_state.pending_join_started_s = snap.pending_join_started_s;
    g_team_state.kicked_out = snap.kicked_out;
    g_team_state.self_is_leader = snap.self_is_leader;
    g_team_state.last_event_seq = snap.last_event_seq;
    g_team_state.team_chat_unread = snap.team_chat_unread;
    g_team_state.team_id = snap.team_id;
    g_team_state.has_team_id = snap.has_team_id;
    g_team_state.team_name = snap.team_name;
    g_team_state.security_round = snap.security_round;
    g_team_state.last_update_s = snap.last_update_s;
    g_team_state.team_psk = snap.team_psk;
    g_team_state.has_team_psk = snap.has_team_psk;
    g_team_state.members = snap.members;
    ensure_member_colors();
}

bool is_team_ui_active()
{
    return g_team_state.root && lv_obj_is_valid(g_team_state.root);
}

bool is_team_chat_visible()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    chat::ui::UiController* ui = app_ctx.getUiController();
    if (!ui)
    {
        return false;
    }
    auto state = ui->getState();
    if (state != chat::ui::UiController::State::Conversation &&
        state != chat::ui::UiController::State::Compose)
    {
        return false;
    }
    return ui->isTeamConversationActive();
}

bool is_pairing_active()
{
    return g_team_state.pairing_state != TeamPairingState::Idle &&
           g_team_state.pairing_state != TeamPairingState::Completed &&
           g_team_state.pairing_state != TeamPairingState::Failed;
}

void sync_pairing_from_service()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamPairingService* pairing = app_ctx.getTeamPairing();
    if (!pairing)
    {
        return;
    }
    TeamPairingStatus status = pairing->getStatus();
    g_team_state.pairing_role = status.role;
    g_team_state.pairing_state = status.state;
    g_team_state.pairing_team_name.clear();
    if (status.has_team_name)
    {
        g_team_state.pairing_team_name = status.team_name;
    }
    if (status.has_team_id)
    {
        g_team_state.team_id = status.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(status.team_id);
    }
    if (is_pairing_active())
    {
        g_team_state.pending_join = true;
        if (g_team_state.pending_join_started_s == 0)
        {
            g_team_state.pending_join_started_s = now_secs();
        }
    }
    else
    {
        g_team_state.pending_join = false;
        g_team_state.pending_join_started_s = 0;
    }
}

void load_state_from_store()
{
    if (s_state_loaded)
    {
        return;
    }
    TeamUiSnapshot snap;
    if (team_ui_get_store().load(snap))
    {
        apply_snapshot(snap);
    }
    s_state_loaded = true;
}

void save_state_to_store()
{
    TeamUiSnapshot snap = snapshot_from_state();
    team_ui_get_store().save(snap);
}

void update_team_name_from_id(const TeamId& id)
{
    g_team_state.team_name = format_team_name_from_id(id);
}

void fill_status_members(team::proto::TeamStatus& status)
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    uint32_t self_id = app_ctx.getSelfNodeId();
    status.members.clear();
    status.leader_id = 0;
    for (const auto& m : g_team_state.members)
    {
        uint32_t id = (m.node_id == 0) ? self_id : m.node_id;
        if (id == 0)
        {
            continue;
        }
        if (std::find(status.members.begin(), status.members.end(), id) == status.members.end())
        {
            status.members.push_back(id);
        }
        if (m.leader)
        {
            status.leader_id = id;
        }
    }
    if (status.leader_id == 0 && g_team_state.self_is_leader && self_id != 0)
    {
        status.leader_id = self_id;
    }
    status.has_members = !status.members.empty();
}

void apply_member_list_from_status(const team::proto::TeamStatus& status)
{
    if (!status.has_members)
    {
        return;
    }
    app::AppContext& app_ctx = app::AppContext::getInstance();
    uint32_t self_id = app_ctx.getSelfNodeId();

    auto find_existing = [&](uint32_t node_id) -> const TeamMemberUi*
    {
        for (const auto& m : g_team_state.members)
        {
            if (m.node_id == node_id)
            {
                return &m;
            }
        }
        return nullptr;
    };

    std::vector<TeamMemberUi> updated;
    updated.reserve(status.members.size() + 1);
    for (uint32_t id : status.members)
    {
        if (id == 0)
        {
            continue;
        }
        uint32_t node_id = (id == self_id) ? 0 : id;
        TeamMemberUi entry;
        if (const TeamMemberUi* existing = find_existing(node_id))
        {
            entry = *existing;
        }
        entry.node_id = node_id;
        entry.leader = (id == status.leader_id);
        if (entry.name.empty())
        {
            entry.name = (node_id == 0) ? "You" : resolve_node_name(id);
        }
        if (entry.last_seen_s == 0)
        {
            entry.last_seen_s = now_secs();
        }
        assign_member_color(entry);
        updated.push_back(entry);
    }

    bool has_self = std::any_of(updated.begin(), updated.end(),
                                [](const TeamMemberUi& m)
                                {
                                    return m.node_id == 0;
                                });
    if (!has_self && self_id != 0)
    {
        TeamMemberUi self;
        self.node_id = 0;
        self.name = "You";
        self.leader = (status.leader_id == self_id);
        self.last_seen_s = now_secs();
        assign_member_color(self);
        updated.push_back(self);
    }

    g_team_state.members = std::move(updated);
    g_team_state.self_is_leader = (status.leader_id != 0 && status.leader_id == self_id);
}

void handle_team_error(const team::TeamErrorEvent& ev)
{
    if (!g_team_state.in_team || g_team_state.self_is_leader)
    {
        return;
    }
    if (g_team_state.has_team_id && ev.ctx.team_id != g_team_state.team_id)
    {
        return;
    }
    if (ev.error == team::TeamProtocolError::DecryptFail ||
        ev.error == team::TeamProtocolError::KeyMismatch)
    {
        if (!g_team_state.waiting_new_keys)
        {
            ::ui::SystemNotification::show("Team keys mismatch", 2000);
        }
        g_team_state.waiting_new_keys = true;
    }
}

void handle_team_status(const team::TeamStatusEvent& ev)
{
    if (g_team_state.has_team_id && ev.ctx.team_id != g_team_state.team_id)
    {
        return;
    }
    uint32_t prev_round = g_team_state.security_round;
    if (!g_team_state.has_team_id)
    {
        g_team_state.team_id = ev.ctx.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(ev.ctx.team_id);
    }
    if (ev.ctx.key_id != 0)
    {
        g_team_state.security_round = ev.ctx.key_id;
    }
    apply_member_list_from_status(ev.msg);
    if (ev.msg.key_id != 0)
    {
        if (ev.msg.key_id > g_team_state.security_round)
        {
            g_team_state.waiting_new_keys = true;
        }
        else if (ev.msg.key_id == g_team_state.security_round)
        {
            g_team_state.waiting_new_keys = false;
        }
        if (ev.ctx.from != 0)
        {
            mark_keydist_confirmed(ev.ctx.from, ev.msg.key_id);
        }
    }
    if (ev.msg.key_id != 0 && ev.msg.key_id > prev_round)
    {
        std::vector<uint8_t> payload;
        write_u32_le(payload, ev.msg.key_id);
        append_key_event(TeamKeyEventType::EpochRotated, payload);
    }
    g_team_state.last_update_s = ev.ctx.timestamp;
}

void handle_team_position(const team::TeamPositionEvent& ev)
{
    if (g_team_state.has_team_id && ev.ctx.team_id != g_team_state.team_id)
    {
        return;
    }
    if (!g_team_state.has_team_id)
    {
        g_team_state.team_id = ev.ctx.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(ev.ctx.team_id);
    }
    touch_member(ev.ctx.from, ev.ctx.timestamp);
    if (ev.ctx.key_id != 0 && ev.ctx.from != 0)
    {
        mark_keydist_confirmed(ev.ctx.from, ev.ctx.key_id);
    }
    if (!ev.payload.empty())
    {
        meshtastic_Position pos = meshtastic_Position_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(ev.payload.data(), ev.payload.size());
        if (pb_decode(&stream, meshtastic_Position_fields, &pos))
        {
            if (pos.has_latitude_i && pos.has_longitude_i)
            {
                int32_t lat_e7 = pos.latitude_i;
                int32_t lon_e7 = pos.longitude_i;
                int16_t alt_m = 0;
                if (pos.has_altitude)
                {
                    if (pos.altitude > 32767) alt_m = 32767;
                    else if (pos.altitude < -32768) alt_m = -32768;
                    else alt_m = static_cast<int16_t>(pos.altitude);
                }
                uint16_t speed_dmps = 0;
                if (pos.has_ground_speed)
                {
                    double dmps = static_cast<double>(pos.ground_speed) * 10.0;
                    if (dmps < 0.0) dmps = 0.0;
                    if (dmps > 65535.0) dmps = 65535.0;
                    speed_dmps = static_cast<uint16_t>(lround(dmps));
                }
                uint32_t ts = pos.timestamp != 0 ? pos.timestamp : ev.ctx.timestamp;
                if (ts == 0)
                {
                    ts = now_secs();
                }
                team_ui_posring_append(g_team_state.team_id,
                                       ev.ctx.from,
                                       lat_e7,
                                       lon_e7,
                                       alt_m,
                                       speed_dmps,
                                       ts);
            }
        }
    }
    g_team_state.last_update_s = ev.ctx.timestamp;
}

void handle_team_waypoint(const team::TeamWaypointEvent& ev)
{
    if (g_team_state.has_team_id && ev.ctx.team_id != g_team_state.team_id)
    {
        return;
    }
    if (!g_team_state.has_team_id)
    {
        g_team_state.team_id = ev.ctx.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(ev.ctx.team_id);
    }
    touch_member(ev.ctx.from, ev.ctx.timestamp);
    if (ev.ctx.key_id != 0 && ev.ctx.from != 0)
    {
        mark_keydist_confirmed(ev.ctx.from, ev.ctx.key_id);
    }
    g_team_state.last_update_s = ev.ctx.timestamp;
}

void handle_team_track(const team::TeamTrackEvent& ev)
{
    if (g_team_state.has_team_id && ev.ctx.team_id != g_team_state.team_id)
    {
        return;
    }
    if (!g_team_state.has_team_id)
    {
        g_team_state.team_id = ev.ctx.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(ev.ctx.team_id);
    }
    touch_member(ev.ctx.from, ev.ctx.timestamp);
    if (ev.ctx.key_id != 0 && ev.ctx.from != 0)
    {
        mark_keydist_confirmed(ev.ctx.from, ev.ctx.key_id);
    }
    if (!ev.payload.empty())
    {
        team::proto::TeamTrackMessage track{};
        if (team::proto::decodeTeamTrackMessage(ev.payload.data(), ev.payload.size(), &track))
        {
            if (track.version == team::proto::kTeamTrackVersion)
            {
                uint32_t base_ts = track.start_ts != 0 ? track.start_ts : ev.ctx.timestamp;
                if (base_ts == 0)
                {
                    base_ts = now_secs();
                }
                for (size_t i = 0; i < track.points.size(); ++i)
                {
                    if ((track.valid_mask & (1u << static_cast<uint32_t>(i))) == 0)
                    {
                        continue;
                    }
                    const auto& pt = track.points[i];
                    uint32_t ts = base_ts + static_cast<uint32_t>(track.interval_s) * static_cast<uint32_t>(i);
                    team_ui_posring_append(g_team_state.team_id,
                                           ev.ctx.from,
                                           pt.lat_e7,
                                           pt.lon_e7,
                                           0,
                                           0,
                                           ts);
                }
                team_ui_append_member_track(g_team_state.team_id, ev.ctx.from, track);
                if (g_gps_state.selected_member_id == ev.ctx.from)
                {
                    std::string track_path;
                    if (team_ui_get_member_track_path(g_team_state.team_id, ev.ctx.from, track_path))
                    {
                        gps_tracker_load_file(track_path.c_str(), false);
                    }
                }
            }
        }
    }
    g_team_state.last_update_s = ev.ctx.timestamp;
}

void handle_team_chat(const team::TeamChatEvent& ev)
{
    if (g_team_state.has_team_id && ev.ctx.team_id != g_team_state.team_id)
    {
        return;
    }
    if (!g_team_state.has_team_id)
    {
        g_team_state.team_id = ev.ctx.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(ev.ctx.team_id);
    }
    app::AppContext& app_ctx = app::AppContext::getInstance();
    uint32_t from_id = ev.msg.header.from != 0 ? ev.msg.header.from : ev.ctx.from;
    touch_member(from_id, ev.ctx.timestamp);
    if (ev.ctx.key_id != 0 && from_id != 0)
    {
        mark_keydist_confirmed(from_id, ev.ctx.key_id);
    }
    uint32_t ts = ev.msg.header.ts != 0 ? ev.msg.header.ts : ev.ctx.timestamp;
    if (ts == 0)
    {
        ts = now_secs();
    }
    team_ui_chatlog_append_structured(g_team_state.team_id,
                                      from_id,
                                      true,
                                      ts,
                                      ev.msg.header.type,
                                      ev.msg.payload);
    uint32_t prev_unread = g_team_state.team_chat_unread;
    bool incoming = (from_id != 0 && from_id != app_ctx.getSelfNodeId());
    if (incoming && !is_team_chat_visible())
    {
        if (g_team_state.team_chat_unread < UINT32_MAX)
        {
            g_team_state.team_chat_unread += 1;
        }
    }
    else if (is_team_chat_visible())
    {
        g_team_state.team_chat_unread = 0;
    }
    if (g_team_state.team_chat_unread != prev_unread)
    {
        sys::EventBus::publish(new sys::ChatUnreadChangedEvent(2,
                                                               static_cast<int>(g_team_state.team_chat_unread)),
                               0);
    }
    if (ev.msg.header.type == team::proto::TeamChatType::Location)
    {
        team::proto::TeamChatLocation loc;
        if (team::proto::decodeTeamChatLocation(ev.msg.payload.data(),
                                                ev.msg.payload.size(),
                                                &loc))
        {
            uint32_t pos_ts = loc.ts != 0 ? loc.ts : ts;
            team_ui_posring_append(g_team_state.team_id,
                                   from_id,
                                   loc.lat_e7,
                                   loc.lon_e7,
                                   loc.alt_m,
                                   0,
                                   pos_ts);
        }
    }
    g_team_state.last_update_s = ev.ctx.timestamp;
}

void handle_team_kick(const team::TeamKickEvent& ev)
{
    uint32_t target = ev.msg.target;
    int idx = find_member_index(target);
    if (idx >= 0)
    {
        g_team_state.members.erase(g_team_state.members.begin() + idx);
    }
    {
        std::vector<uint8_t> payload;
        write_u32_le(payload, target);
        append_key_event(TeamKeyEventType::MemberKicked, payload);
    }
    g_team_state.security_round += 1;
    if (g_team_state.security_round != 0)
    {
        std::vector<uint8_t> payload;
        write_u32_le(payload, g_team_state.security_round);
        append_key_event(TeamKeyEventType::EpochRotated, payload);
    }

    if (target == 0)
    {
        enter_kicked_out_state();
    }
}

void handle_team_transfer_leader(const team::TeamTransferLeaderEvent& ev)
{
    uint32_t target = ev.msg.target;
    {
        std::vector<uint8_t> payload;
        write_u32_le(payload, target);
        append_key_event(TeamKeyEventType::LeaderTransferred, payload);
    }
    for (auto& m : g_team_state.members)
    {
        m.leader = false;
    }
    int idx = find_member_index(target);
    if (idx < 0)
    {
        TeamMemberUi info;
        info.node_id = target;
        info.name = resolve_node_name(target);
        info.leader = true;
        info.last_seen_s = now_secs();
        g_team_state.members.push_back(info);
    }
    else
    {
        g_team_state.members[idx].leader = true;
    }
    g_team_state.self_is_leader = (target == 0);
}

void handle_team_key_dist(const team::TeamKeyDistEvent& ev)
{
    g_team_state.team_id = ev.msg.team_id;
    g_team_state.has_team_id = true;
    update_team_name_from_id(ev.msg.team_id);
    if (ev.msg.key_id != 0)
    {
        g_team_state.security_round = ev.msg.key_id;
    }
    if (ev.msg.channel_psk_len > 0)
    {
        g_team_state.team_psk = ev.msg.channel_psk;
        g_team_state.has_team_psk = true;
        team_ui_save_keys_now(g_team_state.team_id,
                              g_team_state.security_round,
                              g_team_state.team_psk);
    }
    g_team_state.waiting_new_keys = false;
    g_team_state.last_update_s = ev.ctx.timestamp;

    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller && g_team_state.has_team_psk && g_team_state.has_team_id)
    {
        controller->setKeysFromPsk(g_team_state.team_id,
                                   g_team_state.security_round,
                                   g_team_state.team_psk.data(),
                                   g_team_state.team_psk.size());
    }

    bool was_pairing = g_team_state.pending_join || is_pairing_active() ||
                       g_team_state.pairing_role == TeamPairingRole::Member;
    if (!g_team_state.in_team || was_pairing)
    {
        g_team_state.in_team = true;
        g_team_state.kicked_out = false;
        g_team_state.pending_join = false;
        g_team_state.pending_join_started_s = 0;
        if (g_team_state.pairing_role == TeamPairingRole::Member)
        {
            g_team_state.self_is_leader = false;
        }
        if (g_team_state.members.empty())
        {
            TeamMemberUi self;
            self.node_id = 0;
            self.name = "You";
            self.leader = g_team_state.self_is_leader;
            self.last_seen_s = now_secs();
            assign_member_color(self);
            g_team_state.members.push_back(self);
        }
        g_team_state.page = TeamPage::StatusInTeam;
        g_team_state.nav_stack.clear();
    }

    if (!g_team_state.self_is_leader && ev.ctx.from != 0)
    {
        uint32_t leader_id = ev.ctx.from;
        int idx = find_member_index(leader_id);
        if (idx < 0)
        {
            TeamMemberUi leader;
            leader.node_id = leader_id;
            leader.name = resolve_node_name(leader_id);
            leader.leader = true;
            leader.last_seen_s = now_secs();
            assign_member_color(leader);
            g_team_state.members.push_back(leader);
        }
        else
        {
            g_team_state.members[idx].leader = true;
        }
    }
}

void handle_team_pairing(const team::TeamPairingEvent& ev)
{
    Serial.printf("[TeamUI] pairing event role=%u state=%u peer=%08lX in_team=%u leader=%u members=%u\n",
                  static_cast<unsigned>(ev.role),
                  static_cast<unsigned>(ev.state),
                  static_cast<unsigned long>(ev.peer_id),
                  g_team_state.in_team ? 1 : 0,
                  g_team_state.self_is_leader ? 1 : 0,
                  static_cast<unsigned>(g_team_state.members.size()));
    g_team_state.pairing_role = ev.role;
    g_team_state.pairing_state = ev.state;
    g_team_state.pairing_peer_id = ev.peer_id;
    g_team_state.pairing_team_name.clear();
    if (ev.has_team_name)
    {
        g_team_state.pairing_team_name = ev.team_name;
    }
    if (ev.has_team_id)
    {
        g_team_state.team_id = ev.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(ev.team_id);
    }

    if (ev.role == TeamPairingRole::Leader)
    {
        g_team_state.in_team = true;
        g_team_state.kicked_out = false;
        g_team_state.self_is_leader = true;
        int self_idx = find_member_index(0);
        if (self_idx < 0)
        {
            TeamMemberUi self;
            self.node_id = 0;
            self.name = "You";
            self.leader = true;
            self.last_seen_s = now_secs();
            assign_member_color(self);
            g_team_state.members.push_back(self);
        }
        else
        {
            g_team_state.members[self_idx].leader = true;
        }
    }

    if (ev.role == TeamPairingRole::Leader && ev.peer_id != 0 &&
        ev.state == TeamPairingState::LeaderBeacon)
    {
        bool is_new_member = (find_member_index(ev.peer_id) < 0);
        touch_member(ev.peer_id, now_secs());
        g_team_state.last_update_s = now_secs();
        std::vector<uint8_t> payload;
        write_u32_le(payload, ev.peer_id);
        payload.push_back(kKeyRoleMember);
        append_key_event(TeamKeyEventType::MemberAccepted, payload);
        Serial.printf("[TeamUI] leader accept member=%08lX members=%u\n",
                      static_cast<unsigned long>(ev.peer_id),
                      static_cast<unsigned>(g_team_state.members.size()));
        if (is_new_member)
        {
            app::AppContext& app_ctx = app::AppContext::getInstance();
            team::TeamController* controller = app_ctx.getTeamController();
            if (controller && g_team_state.has_team_id)
            {
                team::proto::TeamStatus status{};
                status.key_id = g_team_state.security_round;
                fill_status_members(status);
                if (!controller->onStatus(status, chat::ChannelId::PRIMARY, 0))
                {
                    notify_send_failed("Status", true);
                }
                if (!controller->onStatusPlain(status, chat::ChannelId::PRIMARY, 0))
                {
                    notify_send_failed("Status", false);
                }
                schedule_status_broadcast(1, 2);
            }
        }
        std::string name = resolve_node_name(ev.peer_id);
        std::string msg = "Paired: " + name;
        ::ui::SystemNotification::show(msg.c_str(), 2000);
    }

    if (ev.state == TeamPairingState::Completed)
    {
        g_team_state.pending_join = false;
        g_team_state.pending_join_started_s = 0;
        if (!g_team_state.in_team)
        {
            g_team_state.in_team = true;
            g_team_state.kicked_out = false;
            g_team_state.self_is_leader = (ev.role == TeamPairingRole::Leader);
            if (g_team_state.members.empty())
            {
                TeamMemberUi self;
                self.node_id = 0;
                self.name = "You";
                self.leader = g_team_state.self_is_leader;
                self.last_seen_s = now_secs();
                assign_member_color(self);
                g_team_state.members.push_back(self);
            }
        }
        g_team_state.page = TeamPage::StatusInTeam;
        g_team_state.nav_stack.clear();
        ::ui::SystemNotification::show("Paired successfully", 2000);
        return;
    }
    if (ev.state == TeamPairingState::Failed)
    {
        g_team_state.pending_join = false;
        g_team_state.pending_join_started_s = 0;
        if (!g_team_state.in_team)
        {
            g_team_state.page = TeamPage::StatusNotInTeam;
        }
        else
        {
            g_team_state.page = TeamPage::StatusInTeam;
        }
        g_team_state.nav_stack.clear();
        ::ui::SystemNotification::show("Pairing failed", 2000);
        return;
    }
    if (is_pairing_active())
    {
        g_team_state.pending_join = true;
        if (g_team_state.pending_join_started_s == 0)
        {
            g_team_state.pending_join_started_s = now_secs();
        }
    }
    else
    {
        g_team_state.pending_join = false;
        g_team_state.pending_join_started_s = 0;
    }
}

void nav_to(TeamPage page, bool push = true);
void nav_back();
void render_page();
void handle_page_transition(TeamPage next_page);
void nav_reset(TeamPage page);

void top_bar_back(void*)
{
    nav_back();
}

void nav_to(TeamPage page, bool push)
{
    if (push && g_team_state.page != page)
    {
        g_team_state.nav_stack.push_back(g_team_state.page);
    }
    handle_page_transition(page);
    g_team_state.page = page;
    render_page();
}

void nav_back()
{
    if (g_team_state.page == TeamPage::JoinPending && g_team_state.in_team)
    {
        nav_reset(TeamPage::StatusInTeam);
        return;
    }
    if (!g_team_state.nav_stack.empty())
    {
        TeamPage next_page = g_team_state.nav_stack.back();
        g_team_state.nav_stack.pop_back();
        if (next_page == TeamPage::StatusNotInTeam && g_team_state.in_team)
        {
            next_page = TeamPage::StatusInTeam;
        }
        handle_page_transition(next_page);
        g_team_state.page = next_page;
        render_page();
        return;
    }
    ui_request_exit_to_menu();
}

void nav_reset(TeamPage page)
{
    g_team_state.nav_stack.clear();
    handle_page_transition(page);
    g_team_state.page = page;
    render_page();
}

void handle_page_transition(TeamPage next_page)
{
    (void)next_page;
}

bool start_pairing_leader()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamPairingService* pairing = app_ctx.getTeamPairing();
    if (!pairing || !g_team_state.has_team_id || !g_team_state.has_team_psk)
    {
        ::ui::SystemNotification::show("Pairing not ready", 2000);
        return false;
    }
    std::string team_name = current_team_name();
    bool ok = pairing->startLeader(g_team_state.team_id,
                                   g_team_state.security_round,
                                   g_team_state.team_psk.data(),
                                   g_team_state.team_psk.size(),
                                   app_ctx.getSelfNodeId(),
                                   team_name.c_str());
    if (!ok)
    {
        ::ui::SystemNotification::show("Pairing init failed", 2000);
        return false;
    }
    g_team_state.pending_join = true;
    g_team_state.pending_join_started_s = now_secs();
    g_team_state.pairing_role = TeamPairingRole::Leader;
    g_team_state.pairing_state = TeamPairingState::LeaderBeacon;
    save_state_to_store();
    nav_to(TeamPage::JoinPending);
    return true;
}

bool start_pairing_member()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamPairingService* pairing = app_ctx.getTeamPairing();
    if (!pairing)
    {
        ::ui::SystemNotification::show("Pairing not available", 2000);
        return false;
    }
    bool ok = pairing->startMember(app_ctx.getSelfNodeId());
    if (!ok)
    {
        ::ui::SystemNotification::show("Pairing init failed", 2000);
        return false;
    }
    g_team_state.pending_join = true;
    g_team_state.pending_join_started_s = now_secs();
    g_team_state.pairing_role = TeamPairingRole::Member;
    g_team_state.pairing_state = TeamPairingState::MemberScanning;
    save_state_to_store();
    nav_to(TeamPage::JoinPending);
    return true;
}

void handle_create(lv_event_t*)
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();

    g_team_state.in_team = true;
    g_team_state.pending_join = false;
    g_team_state.pending_join_started_s = 0;
    g_team_state.kicked_out = false;
    g_team_state.self_is_leader = true;
    g_team_state.members.clear();
    if (!g_team_state.has_team_id)
    {
        g_team_state.team_id = generate_team_id();
        g_team_state.has_team_id = true;
        update_team_name_from_id(g_team_state.team_id);
    }
    if (g_team_state.security_round == 0)
    {
        g_team_state.security_round = 1;
    }
    if (!g_team_state.has_team_psk)
    {
        for (size_t i = 0; i < g_team_state.team_psk.size(); ++i)
        {
            g_team_state.team_psk[i] = static_cast<uint8_t>(random(0, 256));
        }
        g_team_state.has_team_psk = true;
    }

    if (g_team_state.has_team_id)
    {
        std::vector<uint8_t> payload;
        write_u64_le(payload, team_id_to_u64(g_team_state.team_id));
        write_u32_le(payload, 0);
        write_u32_le(payload, g_team_state.security_round);
        append_key_event(TeamKeyEventType::TeamCreated, payload);
    }
    if (controller)
    {
        if (!controller->setKeysFromPsk(g_team_state.team_id,
                                        g_team_state.security_round,
                                        g_team_state.team_psk.data(),
                                        g_team_state.team_psk.size()))
        {
            notify_send_failed("Keys", true);
        }
    }
    team_ui_save_keys_now(g_team_state.team_id,
                          g_team_state.security_round,
                          g_team_state.team_psk);
    TeamMemberUi self;
    self.node_id = 0;
    self.name = "You";
    self.leader = true;
    self.last_seen_s = now_secs();
    assign_member_color(self);
    g_team_state.members.push_back(self);
    g_team_state.last_update_s = now_secs();
    save_state_to_store();
    if (!start_pairing_leader())
    {
        nav_reset(TeamPage::StatusInTeam);
    }
}

void handle_join(lv_event_t*)
{
    start_pairing_member();
}

void handle_view_team(lv_event_t*)
{
    nav_to(TeamPage::TeamHome);
}

void handle_invite(lv_event_t*)
{
    if (!g_team_state.self_is_leader)
    {
        ::ui::SystemNotification::show("Only leader can pair", 2000);
        return;
    }
    start_pairing_leader();
}

void perform_leave()
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller)
    {
        controller->clearKeys();
    }
    team::TeamPairingService* pairing = app_ctx.getTeamPairing();
    if (pairing)
    {
        pairing->stop();
    }
    g_team_state.in_team = false;
    g_team_state.pending_join = false;
    g_team_state.pending_join_started_s = 0;
    g_team_state.kicked_out = false;
    g_team_state.self_is_leader = false;
    g_team_state.last_event_seq = 0;
    g_team_state.members.clear();
    g_team_state.has_team_id = false;
    g_team_state.team_name.clear();
    g_team_state.security_round = 0;
    g_team_state.has_team_psk = false;
    g_team_state.waiting_new_keys = false;
    s_keydist_pending.clear();
    s_status_pending.clear();
    save_state_to_store();
    nav_reset(TeamPage::StatusNotInTeam);
}

void handle_leave(lv_event_t*)
{
    if (g_team_state.leave_confirm_modal)
    {
        return;
    }
    modal_prepare_group();
    g_team_state.leave_confirm_modal = create_modal_root(260, 140);
    lv_obj_t* win = lv_obj_get_child(g_team_state.leave_confirm_modal, 0);

    lv_obj_t* title_label = lv_label_create(win);
    lv_label_set_text(title_label, "Leave team?");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* desc_label = lv_label_create(win);
    lv_label_set_text(desc_label, "This clears local keys.");
    lv_obj_align(desc_label, LV_ALIGN_TOP_MID, 0, 28);

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

    lv_obj_t* cancel_btn = lv_btn_create(btn_row);
    lv_obj_set_size(cancel_btn, 90, 28);
    style::apply_button_secondary(cancel_btn);
    lv_obj_t* cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(
        cancel_btn, [](lv_event_t*)
        { close_leave_confirm_modal(); },
        LV_EVENT_CLICKED, nullptr);

    lv_obj_t* leave_btn = lv_btn_create(btn_row);
    lv_obj_set_size(leave_btn, 90, 28);
    style::apply_button_secondary(leave_btn);
    lv_obj_t* leave_label = lv_label_create(leave_btn);
    lv_label_set_text(leave_label, "Leave");
    lv_obj_center(leave_label);
    lv_obj_add_event_cb(
        leave_btn, [](lv_event_t*)
        {
        close_leave_confirm_modal();
        perform_leave(); },
        LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g_team_state.modal_group, cancel_btn);
    lv_group_add_obj(g_team_state.modal_group, leave_btn);
    lv_group_focus_obj(cancel_btn);
}

void handle_manage(lv_event_t*)
{
    if (!g_team_state.self_is_leader)
    {
        ::ui::SystemNotification::show("Only leader can manage", 2000);
        return;
    }
    nav_to(TeamPage::Members);
}

void handle_member_clicked(lv_event_t* e)
{
    lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(item);
    g_team_state.selected_member_index = index;
    nav_to(TeamPage::MemberDetail);
}

void handle_kick(lv_event_t*)
{
    nav_to(TeamPage::KickConfirm);
}

void handle_kick_confirm(lv_event_t*)
{
    int idx = g_team_state.selected_member_index;
    if (idx >= 0 && idx < (int)g_team_state.members.size())
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        team::TeamController* controller = app_ctx.getTeamController();
        uint32_t kick_target = g_team_state.members[idx].node_id;
        if (controller)
        {
            team::proto::TeamKick kick{};
            kick.target = kick_target;
            if (!controller->onKick(kick, chat::ChannelId::PRIMARY, 0))
            {
                notify_send_failed("Kick", true);
            }
            uint32_t old_key_id = g_team_state.security_round;
            if (old_key_id == 0)
            {
                old_key_id = 1;
            }
            uint32_t new_key_id = old_key_id + 1;

            std::array<uint8_t, team::proto::kTeamChannelPskSize> new_psk{};
            for (size_t i = 0; i < new_psk.size(); ++i)
            {
                new_psk[i] = static_cast<uint8_t>(random(0, 256));
            }

            team::proto::TeamKeyDist kd{};
            kd.team_id = g_team_state.team_id;
            kd.key_id = new_key_id;
            kd.channel_psk_len = static_cast<uint8_t>(new_psk.size());
            kd.channel_psk = new_psk;

            for (const auto& m : g_team_state.members)
            {
                if (m.node_id == 0 || m.node_id == kick_target)
                {
                    continue;
                }
                if (!controller->onKeyDist(kd, chat::ChannelId::PRIMARY, m.node_id))
                {
                    notify_send_failed_detail("KeyDist", controller->getLastSendError());
                }
                add_keydist_pending(m.node_id, new_key_id);
            }

            g_team_state.security_round = new_key_id;
            g_team_state.team_psk = new_psk;
            g_team_state.has_team_psk = true;
            g_team_state.waiting_new_keys = false;
            if (!controller->setKeysFromPsk(g_team_state.team_id,
                                            g_team_state.security_round,
                                            g_team_state.team_psk.data(),
                                            g_team_state.team_psk.size()))
            {
                notify_send_failed("Keys", true);
            }
        }

        g_team_state.members.erase(g_team_state.members.begin() + idx);
        g_team_state.selected_member_index = -1;

        if (controller)
        {
            team::proto::TeamStatus status{};
            status.key_id = g_team_state.security_round;
            fill_status_members(status);
            if (!controller->onStatus(status, chat::ChannelId::PRIMARY, 0))
            {
                notify_send_failed("Status", true);
            }
            if (!controller->onStatusPlain(status, chat::ChannelId::PRIMARY, 0))
            {
                notify_send_failed("Status", false);
            }
        }
    }
    save_state_to_store();
    nav_reset(TeamPage::StatusInTeam);
}

void handle_kick_cancel(lv_event_t*)
{
    nav_back();
}

void handle_transfer_leader(lv_event_t*)
{
    int idx = g_team_state.selected_member_index;
    if (idx >= 0 && idx < (int)g_team_state.members.size())
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        team::TeamController* controller = app_ctx.getTeamController();
        if (controller)
        {
            team::proto::TeamTransferLeader transfer{};
            transfer.target = g_team_state.members[idx].node_id;
            if (!controller->onTransferLeader(transfer, chat::ChannelId::PRIMARY, 0))
            {
                notify_send_failed("Transfer", true);
            }
        }
        for (auto& m : g_team_state.members)
        {
            m.leader = false;
        }
        g_team_state.members[idx].leader = true;
        g_team_state.self_is_leader = false;
        {
            std::vector<uint8_t> payload;
            write_u32_le(payload, g_team_state.members[idx].node_id);
            append_key_event(TeamKeyEventType::LeaderTransferred, payload);
        }
    }
    save_state_to_store();
    nav_reset(TeamPage::TeamHome);
}

void handle_request_keydist(lv_event_t*)
{
    if (!g_team_state.in_team || g_team_state.self_is_leader || !g_team_state.has_team_id)
    {
        return;
    }
    ::ui::SystemNotification::show("Key refresh not implemented", 2000);
}

void handle_join_cancel(lv_event_t*)
{
    g_team_state.pending_join = false;
    g_team_state.pending_join_started_s = 0;
    {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        team::TeamController* controller = app_ctx.getTeamController();
        if (controller)
        {
            controller->resetUiState();
        }
        team::TeamPairingService* pairing = app_ctx.getTeamPairing();
        if (pairing)
        {
            pairing->stop();
        }
    }
    save_state_to_store();
    if (g_team_state.in_team)
    {
        nav_reset(TeamPage::StatusInTeam);
    }
    else
    {
        nav_reset(TeamPage::StatusNotInTeam);
    }
}

void handle_join_retry(lv_event_t*)
{
    if (g_team_state.self_is_leader)
    {
        start_pairing_leader();
    }
    else
    {
        start_pairing_member();
    }
}

void handle_kicked_join(lv_event_t*)
{
    g_team_state.kicked_out = false;
    save_state_to_store();
    nav_reset(TeamPage::StatusNotInTeam);
}

void handle_kicked_ok(lv_event_t*)
{
    g_team_state.kicked_out = false;
    save_state_to_store();
    nav_reset(TeamPage::StatusNotInTeam);
}

void render_status_not_in_team()
{
    update_top_bar_title("Team");

    add_label(g_team_state.body, "You are not in a team", true, false);
    add_label(g_team_state.body, "• No shared map\n• No team awareness", false, true);
    add_label(g_team_state.body, "Keep devices within 5m", false, false);

    int btn_width = kActionBtnWidth2;
    g_team_state.action_btns[0] = create_action_button("Create Team", btn_width, handle_create);
    g_team_state.action_btns[1] = create_action_button("Join Team", btn_width, handle_join);
    register_focus(g_team_state.action_btns[0], true);
    register_focus(g_team_state.action_btns[1]);
}

void render_status_in_team()
{
    update_top_bar_title("Team Status");

    char line[64];
    std::string team_name = current_team_name();
    snprintf(line, sizeof(line), "Team: %s", team_name.c_str());
    add_label(g_team_state.body, line, true, false);
    snprintf(line, sizeof(line), "Role: %s", g_team_state.self_is_leader ? "Leader" : "Member");
    add_label(g_team_state.body, line, false, true);
    snprintf(line, sizeof(line), "Members: %u", (unsigned)g_team_state.members.size());
    add_label(g_team_state.body, line, false, true);
    snprintf(line, sizeof(line), "Online: %u", (unsigned)online_count());
    add_label(g_team_state.body, line, false, true);
    if (g_team_state.security_round == 0)
    {
        snprintf(line, sizeof(line), "KeyId: --");
    }
    else
    {
        snprintf(line, sizeof(line), "KeyId: %u", (unsigned)g_team_state.security_round);
    }
    add_label(g_team_state.body, line, false, true);
    if (g_team_state.security_round == 0)
    {
        snprintf(line, sizeof(line), "Security: OK (Round --)");
    }
    else
    {
        snprintf(line, sizeof(line), "Security: OK (Round %u)", (unsigned)g_team_state.security_round);
    }
    add_label(g_team_state.body, line, false, true);
    if (g_team_state.waiting_new_keys)
    {
        add_label(g_team_state.body, "Waiting for new keys...", false, true);
    }

    add_label(g_team_state.body, "Team Health", true, false);
    std::string last_update = format_last_update(g_team_state.last_update_s);
    std::string health = std::string("● Leader online\n") +
                         "● " + last_update + "\n" +
                         "○ 1 member stale";
    add_label(g_team_state.body, health.c_str(), false, true);

    int width = kActionBtnWidth3;
    g_team_state.action_btns[0] = create_action_button("View Team", width, handle_view_team);
    if (g_team_state.self_is_leader)
    {
        g_team_state.action_btns[1] = create_action_button("Pair Member", width, handle_invite);
    }
    else
    {
        g_team_state.action_btns[1] = create_action_button("Request Keys", width, handle_request_keydist);
    }
    g_team_state.action_btns[2] = create_action_button("Leave", width, handle_leave);
    register_focus(g_team_state.action_btns[0], true);
    register_focus(g_team_state.action_btns[1]);
    register_focus(g_team_state.action_btns[2]);
}

void render_team_home()
{
    update_top_bar_title(g_team_state.self_is_leader ? "Team · Leader" : "Team · Member");

    char line[64];
    std::string team_name = current_team_name();
    snprintf(line, sizeof(line), "Team: %s", team_name.c_str());
    add_label(g_team_state.body, line, true, false);
    snprintf(line, sizeof(line), "Members: %u  Online: %u",
             (unsigned)g_team_state.members.size(), (unsigned)online_count());
    add_label(g_team_state.body, line, false, true);
    if (g_team_state.security_round == 0)
    {
        snprintf(line, sizeof(line), "Security Round: --");
    }
    else
    {
        snprintf(line, sizeof(line), "Security Round: %u", (unsigned)g_team_state.security_round);
    }
    add_label(g_team_state.body, line, false, true);

    add_label(g_team_state.body, "Members", true, false);
    if (g_team_state.members.empty())
    {
        add_label(g_team_state.body, "No members yet", false, true);
    }
    else
    {
        lv_obj_t* row = lv_obj_create(g_team_state.body);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 4, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        for (const auto& m : g_team_state.members)
        {
            lv_obj_t* label = lv_label_create(row);
            lv_label_set_text(label, m.name.c_str());
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(label, LV_PCT(24));
            lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(label, lv_color_hex(team_color_from_index(m.color_index)), 0);
            lv_obj_set_style_pad_hor(label, 4, 0);
            lv_obj_set_style_pad_ver(label, 3, 0);
            lv_obj_set_style_radius(label, 6, 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            if (m.color_index == 3)
            {
                lv_obj_set_style_text_color(label, lv_color_black(), 0);
            }
            else
            {
                lv_obj_set_style_text_color(label, lv_color_white(), 0);
            }
        }
    }

    int width = kActionBtnWidth3;
    g_team_state.action_btns[0] = create_action_button("Pair Member", width, handle_invite);
    g_team_state.action_btns[1] = create_action_button("Manage", width, handle_manage);
    g_team_state.action_btns[2] = create_action_button("Leave", width, handle_leave);
    register_focus(g_team_state.action_btns[0], g_team_state.default_focus == nullptr);
    register_focus(g_team_state.action_btns[1]);
    register_focus(g_team_state.action_btns[2]);
}

void render_join_pending()
{
    lv_obj_set_style_translate_y(g_team_state.body, -5, 0);
    lv_obj_set_style_pad_top(g_team_state.body, 2, 0);
    lv_obj_set_style_pad_bottom(g_team_state.body, 2, 0);
    lv_obj_set_style_pad_row(g_team_state.body, 2, 0);

    const char* title = "Pairing";
    if (g_team_state.pairing_role == TeamPairingRole::Leader)
    {
        title = "Pairing (Leader)";
    }
    else if (g_team_state.pairing_role == TeamPairingRole::Member)
    {
        title = "Pairing (Member)";
    }
    update_top_bar_title(title);

    add_label(g_team_state.body, "Pairing in progress", true, false);
    add_label(g_team_state.body, "Keep devices within 5m", false, true);
    if (g_team_state.pairing_role == TeamPairingRole::Leader)
    {
        add_label(g_team_state.body, "Members", true, false);
        if (g_team_state.members.empty())
        {
            add_label(g_team_state.body, "No members yet", false, true);
        }
        else
        {
            lv_obj_t* row = lv_obj_create(g_team_state.body);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_set_style_pad_column(row, 4, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            for (const auto& m : g_team_state.members)
            {
                lv_obj_t* label = lv_label_create(row);
                lv_label_set_text(label, m.name.c_str());
                lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
                lv_obj_set_width(label, LV_PCT(24));
                lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
                lv_obj_set_style_bg_color(label, lv_color_hex(team_color_from_index(m.color_index)), 0);
                lv_obj_set_style_pad_hor(label, 4, 0);
                lv_obj_set_style_pad_ver(label, 3, 0);
                lv_obj_set_style_radius(label, 6, 0);
                lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
                if (m.color_index == 3)
                {
                    lv_obj_set_style_text_color(label, lv_color_black(), 0);
                }
                else
                {
                    lv_obj_set_style_text_color(label, lv_color_white(), 0);
                }
            }
        }
        if (g_team_state.pairing_peer_id != 0)
        {
            std::string name = resolve_node_name(g_team_state.pairing_peer_id);
            std::string paired = "Last paired: " + name;
            add_label(g_team_state.body, paired.c_str(), false, true);
        }
    }
    if (!g_team_state.pairing_team_name.empty())
    {
        std::string line = "Target: " + g_team_state.pairing_team_name;
        add_label(g_team_state.body, line.c_str(), false, true);
    }

    const char* state_line = "Waiting for handshake...";
    switch (g_team_state.pairing_state)
    {
    case TeamPairingState::LeaderBeacon:
        state_line = "Waiting for member...";
        break;
    case TeamPairingState::MemberScanning:
        state_line = "Scanning for team...";
        break;
    case TeamPairingState::JoinSent:
        state_line = "Join request sent...";
        break;
    case TeamPairingState::WaitingKey:
        state_line = "Waiting for keys...";
        break;
    case TeamPairingState::Completed:
        state_line = "Paired successfully";
        break;
    case TeamPairingState::Failed:
        state_line = "Pairing failed";
        break;
    default:
        break;
    }
    add_label(g_team_state.body, state_line, false, true);

    int width = kActionBtnWidth2;
    g_team_state.action_btns[0] = create_action_button("Cancel", width, handle_join_cancel);
    g_team_state.action_btns[1] = create_action_button("Retry", width, handle_join_retry);
    register_focus(g_team_state.action_btns[0], true);
    register_focus(g_team_state.action_btns[1]);
}

void render_members()
{
    update_top_bar_title("Members");

    if (g_team_state.members.empty())
    {
        add_label(g_team_state.body, "No members yet", false, true);
    }
    for (size_t i = 0; i < g_team_state.members.size(); ++i)
    {
        const auto& m = g_team_state.members[i];
        const char* dot = m.online ? "● " : "○ ";
        std::string left = std::string(dot) + m.name;
        if (m.leader)
        {
            left += " (Leader)";
        }
        lv_obj_t* item = create_list_item(left.c_str(), "Select");
        lv_obj_set_user_data(item, (void*)(intptr_t)i);
        lv_obj_add_event_cb(item, handle_member_clicked, LV_EVENT_CLICKED, nullptr);
        register_focus(item, g_team_state.default_focus == nullptr);
    }
}

void render_member_detail()
{
    if (g_team_state.selected_member_index < 0 ||
        g_team_state.selected_member_index >= (int)g_team_state.members.size())
    {
        nav_to(TeamPage::Members, false);
        return;
    }

    const auto& member = g_team_state.members[g_team_state.selected_member_index];
    std::string title = "Member: " + member.name;
    update_top_bar_title(title.c_str());

    std::string status = member.online ? "Online" : format_last_seen(member.last_seen_s);
    std::string role = member.leader ? "Leader" : "Member";
    char line[64];
    snprintf(line, sizeof(line), "Status: %s", status.c_str());
    add_label(g_team_state.body, line, true, false);
    snprintf(line, sizeof(line), "Role: %s", role.c_str());
    add_label(g_team_state.body, line, false, true);
    add_label(g_team_state.body, "Device: Pager", false, true);
    add_label(g_team_state.body, "Capability:", true, false);
    add_label(g_team_state.body, "- Position\n- Waypoint", false, true);

    int width = kActionBtnWidth2;
    g_team_state.action_btns[0] = create_action_button("Kick", width, handle_kick);
    g_team_state.action_btns[1] = create_action_button("Transfer Leader", width, handle_transfer_leader);
    bool keys_ready = g_team_state.has_team_psk && g_team_state.has_team_id && g_team_state.security_round > 0;
    if (!keys_ready || g_team_state.waiting_new_keys)
    {
        lv_obj_add_state(g_team_state.action_btns[0], LV_STATE_DISABLED);
        lv_obj_add_state(g_team_state.action_btns[1], LV_STATE_DISABLED);
    }
    register_focus(g_team_state.action_btns[0], true);
    register_focus(g_team_state.action_btns[1]);
}

void render_kick_confirm()
{
    update_top_bar_title("Kick Member");

    std::string name = "member";
    if (g_team_state.selected_member_index >= 0 &&
        g_team_state.selected_member_index < (int)g_team_state.members.size())
    {
        name = g_team_state.members[g_team_state.selected_member_index].name;
    }

    std::string line = "Remove " + name + " from team?";
    add_label(g_team_state.body, line.c_str(), true, false);
    add_label(g_team_state.body,
              "This will update the security round.\n"
              "The member will no longer receive\n"
              "team messages or waypoints.",
              false, true);

    int width = kActionBtnWidth2;
    g_team_state.action_btns[0] = create_action_button("Cancel", width, handle_kick_cancel);
    g_team_state.action_btns[1] = create_action_button("Confirm Kick", width, handle_kick_confirm);
    register_focus(g_team_state.action_btns[0], true);
    register_focus(g_team_state.action_btns[1]);
}

void render_kicked_out()
{
    update_top_bar_title("Team");

    add_label(g_team_state.body, "You are no longer in this team", true, false);
    add_label(g_team_state.body, "Access to team data revoked", false, true);

    int width = kActionBtnWidth2;
    g_team_state.action_btns[0] = create_action_button("Pair Again", width, handle_kicked_join);
    g_team_state.action_btns[1] = create_action_button("OK", width, handle_kicked_ok);
    register_focus(g_team_state.action_btns[0], true);
    register_focus(g_team_state.action_btns[1]);
}

void render_page()
{
    clear_content();
    if (g_team_state.body)
    {
        lv_obj_set_style_translate_y(g_team_state.body, 0, 0);
        lv_obj_set_style_pad_top(g_team_state.body, 6, 0);
        lv_obj_set_style_pad_bottom(g_team_state.body, 6, 0);
        lv_obj_set_style_pad_left(g_team_state.body, 6, 0);
        lv_obj_set_style_pad_right(g_team_state.body, 6, 0);
        lv_obj_set_style_pad_row(g_team_state.body, 6, 0);
    }

    switch (g_team_state.page)
    {
    case TeamPage::StatusNotInTeam:
        render_status_not_in_team();
        break;
    case TeamPage::StatusInTeam:
        render_status_in_team();
        break;
    case TeamPage::TeamHome:
        render_team_home();
        break;
    case TeamPage::JoinPending:
        render_join_pending();
        break;
    case TeamPage::Members:
        render_members();
        break;
    case TeamPage::MemberDetail:
        render_member_detail();
        break;
    case TeamPage::KickConfirm:
        render_kick_confirm();
        break;
    case TeamPage::KickedOut:
        render_kicked_out();
        break;
    default:
        render_status_not_in_team();
        break;
    }

    ui_update_top_bar_battery(g_team_state.top_bar_widget);
    refresh_team_input();
}

void sync_from_controller()
{
    sync_pairing_from_service();
    bool pairing_active = is_pairing_active();
    if (pairing_active && g_team_state.pending_join_started_s == 0)
    {
        g_team_state.pending_join_started_s = now_secs();
    }
    if (pairing_active && !g_team_state.in_team && g_team_state.page != TeamPage::JoinPending)
    {
        g_team_state.page = TeamPage::JoinPending;
        g_team_state.nav_stack.clear();
    }
    if (!pairing_active && g_team_state.in_team &&
        (g_team_state.page == TeamPage::StatusNotInTeam ||
         g_team_state.page == TeamPage::JoinPending))
    {
        g_team_state.page = TeamPage::StatusInTeam;
        g_team_state.nav_stack.clear();
    }
    if (!pairing_active && !g_team_state.in_team && g_team_state.page == TeamPage::JoinPending)
    {
        g_team_state.page = TeamPage::StatusNotInTeam;
        g_team_state.nav_stack.clear();
    }
}
} // namespace

void team_page_create(lv_obj_t* parent)
{
    if (g_team_state.root)
    {
        lv_obj_del(g_team_state.root);
        g_team_state.root = nullptr;
    }

    style::init_once();
    load_state_from_store();
    sync_pairing_from_service();

    if (g_team_state.kicked_out)
    {
        g_team_state.page = TeamPage::KickedOut;
    }
    else if (is_pairing_active() || g_team_state.pending_join)
    {
        g_team_state.page = TeamPage::JoinPending;
    }
    else if (g_team_state.in_team)
    {
        g_team_state.page = TeamPage::StatusInTeam;
    }
    else
    {
        g_team_state.page = TeamPage::StatusNotInTeam;
    }

    g_team_state.root = layout::create_root(parent);
    g_team_state.header = layout::create_header(g_team_state.root);
    g_team_state.content = layout::create_content(g_team_state.root);
    g_team_state.body = layout::create_body(g_team_state.content);
    g_team_state.actions = layout::create_actions(g_team_state.content);

    style::apply_root(g_team_state.root);
    style::apply_header(g_team_state.header);
    style::apply_content(g_team_state.content);
    style::apply_body(g_team_state.body);
    style::apply_actions(g_team_state.actions);

    ::ui::widgets::TopBarConfig cfg;
    cfg.height = ::ui::widgets::kTopBarHeight;
    ::ui::widgets::top_bar_init(g_team_state.top_bar_widget, g_team_state.header, cfg);
    update_top_bar_title("Team");
    ::ui::widgets::top_bar_set_back_callback(g_team_state.top_bar_widget, top_bar_back, nullptr);
    ui_update_top_bar_battery(g_team_state.top_bar_widget);

    init_team_input();
    team_page_refresh();
}

void team_page_destroy()
{
    cleanup_team_input();

    close_leave_confirm_modal();
    if (g_team_state.modal_group)
    {
        lv_group_del(g_team_state.modal_group);
        g_team_state.modal_group = nullptr;
    }
    s_keydist_pending.clear();

    if (g_team_state.root)
    {
        lv_obj_del(g_team_state.root);
        g_team_state.root = nullptr;
    }
    g_team_state = TeamPageState{};
    s_state_loaded = false;
}

void team_page_refresh()
{
    sync_from_controller();
    render_page();
}

bool team_page_handle_event(sys::Event* event)
{
    if (!event)
    {
        return false;
    }
    load_state_from_store();

    bool changed = false;

    switch (event->type)
    {
    case sys::EventType::TeamKick:
        handle_team_kick(static_cast<sys::TeamKickEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamTransferLeader:
        handle_team_transfer_leader(static_cast<sys::TeamTransferLeaderEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamKeyDist:
        handle_team_key_dist(static_cast<sys::TeamKeyDistEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamPairing:
        handle_team_pairing(static_cast<sys::TeamPairingEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::SystemTick:
    {
        process_keydist_retries();
        process_status_broadcasts();
        break;
    }
    case sys::EventType::TeamStatus:
        handle_team_status(static_cast<sys::TeamStatusEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamPosition:
        handle_team_position(static_cast<sys::TeamPositionEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamWaypoint:
        handle_team_waypoint(static_cast<sys::TeamWaypointEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamTrack:
        handle_team_track(static_cast<sys::TeamTrackEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamChat:
        handle_team_chat(static_cast<sys::TeamChatEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamError:
        handle_team_error(static_cast<sys::TeamErrorEvent*>(event)->data);
        changed = true;
        break;
    default:
        break;
    }

    if (changed)
    {
        save_state_to_store();
        if (is_team_ui_active())
        {
            team_page_refresh();
        }
    }

    return true;
}

} // namespace ui
} // namespace team

void team_page_create(lv_obj_t* parent)
{
    team::ui::team_page_create(parent);
}

void team_page_destroy()
{
    team::ui::team_page_destroy();
}

void team_page_refresh()
{
    team::ui::team_page_refresh();
}

bool team_page_handle_event(sys::Event* event)
{
    return team::ui::team_page_handle_event(event);
}
