/**
 * @file team_page_components.cpp
 * @brief Team page components
 */

#include "team_page_components.h"
#include "team_page_layout.h"
#include "team_page_styles.h"
#include "team_page_input.h"
#include "team_state.h"
#include "../../ui_common.h"
#include "../../../app/app_context.h"
#include "../../../team/protocol/team_mgmt.h"
#include "../../../team/usecase/team_service.h"
#include "../../../sys/event_bus.h"
#include "../../widgets/system_notification.h"

#include <Arduino.h>
#include <cstdio>
#include <string>
#include <algorithm>

namespace team
{
namespace ui
{
namespace
{
constexpr int kActionBtnHeight = 32;
constexpr int kActionBtnWidth2 = 170;
constexpr int kActionBtnWidth3 = 140;
constexpr int kListItemHeight = 32;
constexpr int kInviteTtlSec = 9 * 60;
constexpr uint8_t kKeyDistMaxRetries = 3;
constexpr uint32_t kKeyDistRetryIntervalSec = 5;
constexpr uint32_t kJoinPendingTimeoutSec = 30;

uint32_t now_secs();

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
                       [&](const KeyDistPending& item) {
                           return item.node_id == node_id && item.key_id == key_id;
                       }),
        s_keydist_pending.end());
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
    uint32_t now = now_secs();
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (!controller)
    {
        return;
    }

    for (auto it = s_keydist_pending.begin(); it != s_keydist_pending.end(); )
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
    g_team_state.in_team = false;
    g_team_state.pending_join = false;
    g_team_state.pending_join_started_s = 0;
    g_team_state.kicked_out = true;
    g_team_state.self_is_leader = false;
    g_team_state.members.clear();
    g_team_state.has_team_id = false;
    g_team_state.team_name.clear();
    g_team_state.security_round = 0;
    g_team_state.has_team_psk = false;
    g_team_state.waiting_new_keys = false;
    g_team_state.has_join_target = false;
    s_keydist_pending.clear();
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
    lv_obj_set_style_bg_color(bg, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bg, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(bg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(bg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* win = lv_obj_create(bg);
    lv_obj_set_size(win, width, height);
    lv_obj_center(win);
    lv_obj_set_style_bg_color(win, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(win, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(win, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_radius(win, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(win, 8, LV_PART_MAIN);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    return bg;
}

void close_join_request_modal()
{
    if (g_team_state.join_request_modal)
    {
        lv_obj_del(g_team_state.join_request_modal);
        g_team_state.join_request_modal = nullptr;
    }
    g_team_state.pending_join_node_id = 0;
    g_team_state.pending_join_name.clear();
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

std::string format_signal(uint8_t bars)
{
    if (bars > 4)
    {
        bars = 4;
    }
    char buf[32];
    const char* full = "▮";
    const char* empty = "▯";
    std::string meter;
    for (uint8_t i = 0; i < 4; ++i)
    {
        meter += (i < bars) ? full : empty;
    }
    snprintf(buf, sizeof(buf), "Signal: %s", meter.c_str());
    return std::string(buf);
}

std::string format_invite_code(const std::string& code)
{
    if (code.empty())
    {
        return "--";
    }
    std::string out;
    out.reserve(code.size() + 2);
    for (char c : code)
    {
        if (c == '-')
        {
            out += " \xE2\x80\x93 "; // en dash
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

uint32_t hash_invite_code(const std::string& code)
{
    uint32_t h = 2166136261u;
    for (char c : code)
    {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

std::string generate_invite_code()
{
    static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    constexpr size_t kAlphaLen = sizeof(kAlphabet) - 1;
    auto pick = []() {
        return kAlphabet[random(0, kAlphaLen)];
    };
    std::string code;
    code.reserve(7);
    code.push_back(pick());
    code.push_back(pick());
    code.push_back(pick());
    code.push_back('-');
    code.push_back(pick());
    code.push_back(pick());
    code.push_back(pick());
    return code;
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

void touch_member(uint32_t node_id, uint32_t last_seen_s)
{
    int idx = find_member_index(node_id);
    if (idx < 0)
    {
        TeamMemberUi info;
        info.node_id = node_id;
        info.name = resolve_node_name(node_id);
        info.last_seen_s = last_seen_s;
        g_team_state.members.push_back(info);
        return;
    }
    g_team_state.members[idx].last_seen_s = last_seen_s;
    g_team_state.members[idx].name = resolve_node_name(node_id);
}

int find_nearby_team(const TeamId& id)
{
    for (size_t i = 0; i < g_team_state.nearby_teams.size(); ++i)
    {
        if (g_team_state.nearby_teams[i].team_id == id)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

TeamUiSnapshot snapshot_from_state()
{
    TeamUiSnapshot snap;
    snap.in_team = g_team_state.in_team;
    snap.pending_join = g_team_state.pending_join;
    snap.pending_join_started_s = g_team_state.pending_join_started_s;
    snap.kicked_out = g_team_state.kicked_out;
    snap.self_is_leader = g_team_state.self_is_leader;
    snap.team_id = g_team_state.team_id;
    snap.has_team_id = g_team_state.has_team_id;
    snap.join_target_id = g_team_state.join_target_id;
    snap.has_join_target = g_team_state.has_join_target;
    snap.team_name = g_team_state.team_name;
    snap.security_round = g_team_state.security_round;
    snap.invite_code = g_team_state.invite_code;
    snap.invite_expires_s = g_team_state.invite_expires_s;
    snap.last_update_s = g_team_state.last_update_s;
    snap.team_psk = g_team_state.team_psk;
    snap.has_team_psk = g_team_state.has_team_psk;
    snap.members = g_team_state.members;
    snap.nearby_teams = g_team_state.nearby_teams;
    return snap;
}

void apply_snapshot(const TeamUiSnapshot& snap)
{
    g_team_state.in_team = snap.in_team;
    g_team_state.pending_join = snap.pending_join;
    g_team_state.pending_join_started_s = snap.pending_join_started_s;
    g_team_state.kicked_out = snap.kicked_out;
    g_team_state.self_is_leader = snap.self_is_leader;
    g_team_state.team_id = snap.team_id;
    g_team_state.has_team_id = snap.has_team_id;
    g_team_state.join_target_id = snap.join_target_id;
    g_team_state.has_join_target = snap.has_join_target;
    g_team_state.team_name = snap.team_name;
    g_team_state.security_round = snap.security_round;
    g_team_state.invite_code = snap.invite_code;
    g_team_state.invite_expires_s = snap.invite_expires_s;
    g_team_state.last_update_s = snap.last_update_s;
    g_team_state.team_psk = snap.team_psk;
    g_team_state.has_team_psk = snap.has_team_psk;
    g_team_state.members = snap.members;
    g_team_state.nearby_teams = snap.nearby_teams;
}

bool is_team_ui_active()
{
    return g_team_state.root && lv_obj_is_valid(g_team_state.root);
}

void load_state_from_store()
{
    static bool s_loaded = false;
    if (s_loaded)
    {
        return;
    }
    TeamUiSnapshot snap;
    if (team_ui_get_store().load(snap))
    {
        apply_snapshot(snap);
    }
    s_loaded = true;
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

void handle_team_advertise(const team::TeamAdvertiseEvent& ev)
{
    int idx = find_nearby_team(ev.msg.team_id);
    NearbyTeamUi info;
    info.team_id = ev.msg.team_id;
    info.name = format_team_name_from_id(ev.msg.team_id);
    info.signal_bars = 0;
    info.last_seen_s = ev.ctx.timestamp;
    if (ev.msg.has_join_hint)
    {
        info.join_hint = ev.msg.join_hint;
        info.has_join_hint = true;
    }
    if (idx < 0)
    {
        g_team_state.nearby_teams.push_back(info);
    }
    else
    {
        g_team_state.nearby_teams[idx] = info;
    }
    g_team_state.last_update_s = ev.ctx.timestamp;
}

void handle_team_join_request(const team::TeamJoinRequestEvent& ev)
{
    if (!g_team_state.in_team || !g_team_state.self_is_leader)
    {
        return;
    }
    if (!is_team_ui_active())
    {
        g_team_state.pending_join_node_id = ev.ctx.from;
        g_team_state.pending_join_name = resolve_node_name(ev.ctx.from);
        return;
    }
    if (g_team_state.join_request_modal)
    {
        return;
    }
    g_team_state.pending_join_node_id = ev.ctx.from;
    g_team_state.pending_join_name = resolve_node_name(ev.ctx.from);

    modal_prepare_group();
    g_team_state.join_request_modal = create_modal_root(280, 150);
    lv_obj_t* win = lv_obj_get_child(g_team_state.join_request_modal, 0);

    std::string title = "Join request";
    lv_obj_t* title_label = lv_label_create(win);
    lv_label_set_text(title_label, title.c_str());
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

    std::string name_line = g_team_state.pending_join_name + " wants to join";
    lv_obj_t* name_label = lv_label_create(win);
    lv_label_set_text(name_label, name_line.c_str());
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 26);

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

    lv_obj_t* accept_btn = lv_btn_create(btn_row);
    lv_obj_set_size(accept_btn, 90, 32);
    style::apply_button_secondary(accept_btn);
    lv_obj_t* accept_label = lv_label_create(accept_btn);
    lv_label_set_text(accept_label, "Accept");
    lv_obj_center(accept_label);
    lv_obj_add_event_cb(accept_btn, [](lv_event_t*) {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        team::TeamController* controller = app_ctx.getTeamController();
        if (controller)
        {
            team::proto::TeamJoinDecision decision;
            decision.accept = true;
            if (!controller->onJoinDecision(decision, chat::ChannelId::PRIMARY,
                                            g_team_state.pending_join_node_id))
            {
                notify_send_failed("Decision", false);
            }

            team::proto::TeamJoinAccept accept{};
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

            // Send KeyDist encrypted with OLD keys before rotating locally.
            for (const auto& m : g_team_state.members)
            {
                if (m.node_id == 0)
                {
                    continue;
                }
                if (!controller->onKeyDist(kd, chat::ChannelId::PRIMARY, m.node_id))
                {
                    notify_send_failed_detail("KeyDist", controller->getLastSendError());
                }
                add_keydist_pending(m.node_id, new_key_id);
            }
            if (!controller->onKeyDist(kd, chat::ChannelId::PRIMARY,
                                       g_team_state.pending_join_node_id))
            {
                notify_send_failed_detail("KeyDist", controller->getLastSendError());
            }
            add_keydist_pending(g_team_state.pending_join_node_id, new_key_id);

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

            accept.key_id = g_team_state.security_round;
            if (g_team_state.has_team_id)
            {
                accept.team_id = g_team_state.team_id;
                accept.has_team_id = true;
            }
            accept.channel_psk_len = static_cast<uint8_t>(g_team_state.team_psk.size());
            accept.channel_psk = g_team_state.team_psk;
            if (!controller->onAcceptJoin(accept, chat::ChannelId::PRIMARY,
                                          g_team_state.pending_join_node_id))
            {
                notify_send_failed("JoinAccept", false);
            }

            team::proto::TeamStatus status{};
            status.key_id = g_team_state.security_round;
            if (!controller->onStatus(status, chat::ChannelId::PRIMARY, 0))
            {
                notify_send_failed("Status", true);
            }
            if (!controller->onStatusPlain(status, chat::ChannelId::PRIMARY, 0))
            {
                notify_send_failed("Status", false);
            }
        }
        touch_member(g_team_state.pending_join_node_id, now_secs());
        g_team_state.last_update_s = now_secs();
        save_state_to_store();
        close_join_request_modal();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* reject_btn = lv_btn_create(btn_row);
    lv_obj_set_size(reject_btn, 90, 32);
    style::apply_button_secondary(reject_btn);
    lv_obj_t* reject_label = lv_label_create(reject_btn);
    lv_label_set_text(reject_label, "Reject");
    lv_obj_center(reject_label);
    lv_obj_add_event_cb(reject_btn, [](lv_event_t*) {
        app::AppContext& app_ctx = app::AppContext::getInstance();
        team::TeamController* controller = app_ctx.getTeamController();
        if (controller)
        {
            team::proto::TeamJoinDecision decision;
            decision.accept = false;
            if (!controller->onJoinDecision(decision, chat::ChannelId::PRIMARY,
                                            g_team_state.pending_join_node_id))
            {
                notify_send_failed("Decision", false);
            }
        }
        close_join_request_modal();
    }, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g_team_state.modal_group, accept_btn);
    lv_group_add_obj(g_team_state.modal_group, reject_btn);
    lv_group_focus_obj(accept_btn);
}

void handle_team_join_accept(const team::TeamJoinAcceptEvent& ev)
{
    g_team_state.in_team = true;
    g_team_state.pending_join = false;
    g_team_state.pending_join_started_s = 0;
    g_team_state.kicked_out = false;
    g_team_state.self_is_leader = false;
    if (ev.msg.has_team_id)
    {
        g_team_state.team_id = ev.msg.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(ev.msg.team_id);
    }
    else if (!g_team_state.has_team_id)
    {
        g_team_state.team_id = ev.ctx.team_id;
        g_team_state.has_team_id = true;
        update_team_name_from_id(ev.ctx.team_id);
    }
    g_team_state.has_join_target = false;
    if (ev.ctx.key_id != 0)
    {
        g_team_state.security_round = ev.ctx.key_id;
    }
    if (ev.msg.key_id != 0)
    {
        g_team_state.security_round = ev.msg.key_id;
    }
    g_team_state.members.clear();
    TeamMemberUi self;
    self.node_id = 0;
    self.name = "You";
    self.leader = false;
    self.last_seen_s = now_secs();
    g_team_state.members.push_back(self);
    g_team_state.last_update_s = ev.ctx.timestamp;
    g_team_state.page = TeamPage::StatusInTeam;
    g_team_state.nav_stack.clear();
    g_team_state.waiting_new_keys = false;

    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller && ev.msg.channel_psk_len > 0 && g_team_state.has_team_id)
    {
        controller->setKeysFromPsk(g_team_state.team_id,
                                   g_team_state.security_round,
                                   ev.msg.channel_psk.data(),
                                   ev.msg.channel_psk_len);
    }

    if (controller)
    {
        team::proto::TeamJoinConfirm confirm{};
        confirm.ok = true;
        if (!controller->onConfirmJoin(confirm, chat::ChannelId::PRIMARY, 0))
        {
            notify_send_failed("JoinConfirm", true);
        }
    }
}

void handle_team_join_confirm(const team::TeamJoinConfirmEvent& ev)
{
    if (!g_team_state.in_team)
    {
        return;
    }
    if (ev.msg.ok)
    {
        touch_member(ev.ctx.from, ev.ctx.timestamp);
        g_team_state.last_update_s = ev.ctx.timestamp;
    }
}

void handle_team_join_decision(const team::TeamJoinDecisionEvent& ev)
{
    if (ev.msg.accept)
    {
        return;
    }
    if (g_team_state.pending_join)
    {
        g_team_state.pending_join = false;
        g_team_state.pending_join_started_s = 0;
        g_team_state.has_join_target = false;
        g_team_state.page = TeamPage::JoinTeam;
        g_team_state.nav_stack.clear();
        ::ui::SystemNotification::show("Join rejected", 2000);
    }
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
        enter_kicked_out_state();
    }
}

void handle_team_status(const team::TeamStatusEvent& ev)
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
    if (ev.ctx.key_id != 0)
    {
        g_team_state.security_round = ev.ctx.key_id;
    }
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

void handle_team_kick(const team::TeamKickEvent& ev)
{
    uint32_t target = ev.msg.target;
    int idx = find_member_index(target);
    if (idx >= 0)
    {
        g_team_state.members.erase(g_team_state.members.begin() + idx);
    }
    g_team_state.security_round += 1;

    if (target == 0)
    {
        enter_kicked_out_state();
    }
}

void handle_team_transfer_leader(const team::TeamTransferLeaderEvent& ev)
{
    uint32_t target = ev.msg.target;
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
}

void nav_to(TeamPage page, bool push = true);
void nav_back();
void render_page();

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
    g_team_state.page = page;
    render_page();
}

void nav_back()
{
    if (!g_team_state.nav_stack.empty())
    {
        g_team_state.page = g_team_state.nav_stack.back();
        g_team_state.nav_stack.pop_back();
        render_page();
        return;
    }
    reset_team_ui_state();
    team_page_destroy();
    menu_show();
}

void nav_reset(TeamPage page)
{
    g_team_state.nav_stack.clear();
    g_team_state.page = page;
    render_page();
}

void handle_create(lv_event_t*)
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller)
    {
        team::proto::TeamAdvertise advertise;
        if (!controller->onCreateTeam(advertise, chat::ChannelId::PRIMARY))
        {
            notify_send_failed("Create", false);
        }
    }

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
    TeamMemberUi self;
    self.node_id = 0;
    self.name = "You";
    self.leader = true;
    self.last_seen_s = now_secs();
    g_team_state.members.push_back(self);
    g_team_state.last_update_s = now_secs();
    save_state_to_store();
    nav_reset(TeamPage::StatusInTeam);
}

void handle_join(lv_event_t*)
{
    nav_to(TeamPage::JoinTeam);
}

void handle_view_team(lv_event_t*)
{
    nav_to(TeamPage::TeamHome);
}

void handle_invite(lv_event_t*)
{
    if (!g_team_state.self_is_leader)
    {
        ::ui::SystemNotification::show("Only leader can invite", 2000);
        return;
    }
    if (g_team_state.invite_code.empty())
    {
        g_team_state.invite_code = generate_invite_code();
        g_team_state.invite_expires_s = kInviteTtlSec;
    }
    nav_to(TeamPage::Invite);
}

void handle_leave(lv_event_t*)
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller)
    {
        controller->clearKeys();
    }
    g_team_state.in_team = false;
    g_team_state.pending_join = false;
    g_team_state.pending_join_started_s = 0;
    g_team_state.kicked_out = false;
    g_team_state.self_is_leader = false;
    g_team_state.members.clear();
    g_team_state.has_team_id = false;
    g_team_state.team_name.clear();
    g_team_state.security_round = 0;
    g_team_state.has_team_psk = false;
    g_team_state.waiting_new_keys = false;
    s_keydist_pending.clear();
    save_state_to_store();
    nav_reset(TeamPage::StatusNotInTeam);
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
        if (controller)
        {
            team::proto::TeamKick kick{};
            kick.target = g_team_state.members[idx].node_id;
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
                if (m.node_id == 0 || m.node_id == kick.target)
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

            team::proto::TeamStatus status{};
            status.key_id = g_team_state.security_round;
            if (!controller->onStatus(status, chat::ChannelId::PRIMARY, 0))
            {
                notify_send_failed("Status", true);
            }
            if (!controller->onStatusPlain(status, chat::ChannelId::PRIMARY, 0))
            {
                notify_send_failed("Status", false);
            }
        }
        g_team_state.members.erase(g_team_state.members.begin() + idx);
        g_team_state.selected_member_index = -1;
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
    }
    save_state_to_store();
    nav_reset(TeamPage::TeamHome);
}

void handle_invite_refresh(lv_event_t*)
{
    g_team_state.invite_code = generate_invite_code();
    g_team_state.invite_expires_s = kInviteTtlSec;

    if (!g_team_state.has_team_id)
    {
        g_team_state.team_id = generate_team_id();
        g_team_state.has_team_id = true;
        update_team_name_from_id(g_team_state.team_id);
    }
    if (!g_team_state.has_team_psk)
    {
        for (size_t i = 0; i < g_team_state.team_psk.size(); ++i)
        {
            g_team_state.team_psk[i] = static_cast<uint8_t>(random(0, 256));
        }
        g_team_state.has_team_psk = true;
    }

    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    if (controller)
    {
        if (!controller->setKeysFromPsk(g_team_state.team_id,
                                        g_team_state.security_round ? g_team_state.security_round : 1,
                                        g_team_state.team_psk.data(),
                                        g_team_state.team_psk.size()))
        {
            notify_send_failed("Keys", true);
        }
        team::proto::TeamAdvertise adv{};
        adv.team_id = g_team_state.team_id;
        adv.has_join_hint = true;
        adv.join_hint = hash_invite_code(g_team_state.invite_code);
        adv.has_expires_at = true;
        adv.expires_at = now_secs() + g_team_state.invite_expires_s;
        adv.nonce = random(0, 0xFFFFFFFF);
        if (!controller->onAdvertise(adv, chat::ChannelId::PRIMARY, 0))
        {
            notify_send_failed("Invite", false);
        }
    }
    g_team_state.last_update_s = now_secs();
    save_state_to_store();
    render_page();
}

void handle_invite_stop(lv_event_t*)
{
    g_team_state.invite_code.clear();
    g_team_state.invite_expires_s = 0;
    save_state_to_store();
    nav_reset(TeamPage::TeamHome);
}

void handle_join_enter_code(lv_event_t*)
{
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    bool ok = false;
    if (controller)
    {
        team::proto::TeamJoinRequest join_request;
        if (g_team_state.has_join_target)
        {
            join_request.team_id = g_team_state.join_target_id;
        }
        else if (g_team_state.has_team_id)
        {
            join_request.team_id = g_team_state.team_id;
        }
        join_request.nonce = random(0, 0xFFFFFFFF);
        ok = controller->onJoinTeam(join_request, chat::ChannelId::PRIMARY, 0);
        if (!ok)
        {
            notify_send_failed("Join", false);
        }
    }
    if (ok)
    {
        g_team_state.pending_join = true;
        g_team_state.pending_join_started_s = now_secs();
        g_team_state.has_join_target = false;
        g_team_state.last_update_s = now_secs();
        save_state_to_store();
        nav_to(TeamPage::JoinPending);
    }
}

void handle_join_nearby(lv_event_t* e)
{
    lv_obj_t* item = (lv_obj_t*)lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(item);
    if (index >= 0 && index < (int)g_team_state.nearby_teams.size())
    {
        g_team_state.team_name = g_team_state.nearby_teams[index].name;
        g_team_state.join_target_id = g_team_state.nearby_teams[index].team_id;
        g_team_state.has_join_target = true;
    }
    handle_join_enter_code(nullptr);
}

void handle_join_refresh(lv_event_t*)
{
    render_page();
}

void handle_join_cancel(lv_event_t*)
{
    g_team_state.pending_join = false;
    g_team_state.pending_join_started_s = 0;
    save_state_to_store();
    nav_back();
}

void handle_join_retry(lv_event_t*)
{
    handle_join_enter_code(nullptr);
}

void handle_kicked_join(lv_event_t*)
{
    g_team_state.kicked_out = false;
    save_state_to_store();
    nav_reset(TeamPage::JoinTeam);
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
    add_label(g_team_state.body, "Create or join a team", false, false);

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
    g_team_state.action_btns[1] = create_action_button("Invite", width, handle_invite);
    g_team_state.action_btns[2] = create_action_button("Leave", width, handle_leave);
    bool keys_ready = g_team_state.has_team_psk && g_team_state.has_team_id && g_team_state.security_round > 0;
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
    for (const auto& m : g_team_state.members)
    {
        const char* dot = m.online ? "● " : "○ ";
        std::string left = std::string(dot) + m.name;
        if (m.leader)
        {
            left += " (Leader)";
        }
        std::string right = m.online ? "Online" : format_last_seen(m.last_seen_s);
        (void)create_list_item(left.c_str(), right.c_str());
    }

    int width = kActionBtnWidth3;
    g_team_state.action_btns[0] = create_action_button("Invite", width, handle_invite);
    g_team_state.action_btns[1] = create_action_button("Manage", width, handle_manage);
    g_team_state.action_btns[2] = create_action_button("Leave", width, handle_leave);
    bool keys_ready = g_team_state.has_team_psk && g_team_state.has_team_id && g_team_state.security_round > 0;
    if (!keys_ready || g_team_state.waiting_new_keys)
    {
        lv_obj_add_state(g_team_state.action_btns[0], LV_STATE_DISABLED);
        lv_obj_add_state(g_team_state.action_btns[1], LV_STATE_DISABLED);
    }
    register_focus(g_team_state.action_btns[0], g_team_state.default_focus == nullptr);
    register_focus(g_team_state.action_btns[1]);
    register_focus(g_team_state.action_btns[2]);
}

void render_invite()
{
    update_top_bar_title("Invite");

    add_label(g_team_state.body, "Invite Code", true, false);
    std::string pretty_code = format_invite_code(g_team_state.invite_code);
    add_label(g_team_state.body, pretty_code.c_str(), false, false);
    char line[32];
    if (g_team_state.invite_expires_s == 0)
    {
        snprintf(line, sizeof(line), "Expires in: --:--");
    }
    else
    {
        unsigned minutes = g_team_state.invite_expires_s / 60;
        unsigned seconds = g_team_state.invite_expires_s % 60;
        snprintf(line, sizeof(line), "Expires in: %02u:%02u", minutes, seconds);
    }
    add_label(g_team_state.body, line, false, true);
    add_label(g_team_state.body, "Nearby devices can join", false, true);

    int width = kActionBtnWidth2;
    g_team_state.action_btns[0] = create_action_button("Refresh Code", width, handle_invite_refresh);
    g_team_state.action_btns[1] = create_action_button("Stop Invite", width, handle_invite_stop);
    register_focus(g_team_state.action_btns[0], true);
    register_focus(g_team_state.action_btns[1]);
}

void render_join_team()
{
    update_top_bar_title("Join Team");

    add_label(g_team_state.body, "Nearby Teams", true, false);
    if (g_team_state.nearby_teams.empty())
    {
        add_label(g_team_state.body, "No nearby teams", false, true);
    }
    for (size_t i = 0; i < g_team_state.nearby_teams.size(); ++i)
    {
        const auto& team = g_team_state.nearby_teams[i];
        std::string right = format_signal(team.signal_bars);
        lv_obj_t* item = create_list_item(team.name.c_str(), right.c_str());
        lv_obj_set_user_data(item, (void*)(intptr_t)i);
        lv_obj_add_event_cb(item, handle_join_nearby, LV_EVENT_CLICKED, nullptr);
        register_focus(item, g_team_state.default_focus == nullptr);
    }

    add_label(g_team_state.body, "Or enter invite code", false, true);

    int width = kActionBtnWidth2;
    g_team_state.action_btns[0] = create_action_button("Enter Code", width, handle_join_enter_code);
    g_team_state.action_btns[1] = create_action_button("Refresh", width, handle_join_refresh);
    register_focus(g_team_state.action_btns[0], g_team_state.default_focus == nullptr);
    register_focus(g_team_state.action_btns[1]);
}

void render_join_pending()
{
    update_top_bar_title("Join Request");

    char line[64];
    std::string team_name = current_team_name();
    const char* target = team_name.c_str();
    if (g_team_state.has_join_target)
    {
        static std::string name;
        name = format_team_name_from_id(g_team_state.join_target_id);
        target = name.c_str();
    }
    snprintf(line, sizeof(line), "Request sent to %s", target);
    add_label(g_team_state.body, line, true, false);
    add_label(g_team_state.body, "Waiting for approval...", false, true);
    add_label(g_team_state.body, "This may take a moment", false, true);

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
    g_team_state.action_btns[0] = create_action_button("Join Another Team", width, handle_kicked_join);
    g_team_state.action_btns[1] = create_action_button("OK", width, handle_kicked_ok);
    register_focus(g_team_state.action_btns[0], true);
    register_focus(g_team_state.action_btns[1]);
}

void render_page()
{
    clear_content();

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
    case TeamPage::Invite:
        render_invite();
        break;
    case TeamPage::JoinTeam:
        render_join_team();
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
    app::AppContext& app_ctx = app::AppContext::getInstance();
    team::TeamController* controller = app_ctx.getTeamController();
    team::TeamUiState state = controller ? controller->getState() : team::TeamUiState::Idle;

    if (state == team::TeamUiState::Idle)
    {
        if (g_team_state.pending_join)
        {
            g_team_state.pending_join = false;
            g_team_state.pending_join_started_s = 0;
            g_team_state.nav_stack.clear();
            if (!g_team_state.in_team)
            {
                g_team_state.page = TeamPage::StatusNotInTeam;
            }
        }
        return;
    }

    if (state == team::TeamUiState::PendingJoin)
    {
        g_team_state.pending_join = true;
        if (g_team_state.pending_join_started_s == 0)
        {
            g_team_state.pending_join_started_s = now_secs();
        }
        if (g_team_state.page != TeamPage::JoinPending)
        {
            g_team_state.page = TeamPage::JoinPending;
            g_team_state.nav_stack.clear();
        }
        return;
    }

    if (state == team::TeamUiState::Active)
    {
        g_team_state.in_team = true;
        g_team_state.pending_join = false;
        g_team_state.pending_join_started_s = 0;
        if (g_team_state.page == TeamPage::StatusNotInTeam ||
            g_team_state.page == TeamPage::JoinPending)
        {
            g_team_state.page = TeamPage::StatusInTeam;
            g_team_state.nav_stack.clear();
        }
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

    if (g_team_state.kicked_out)
    {
        g_team_state.page = TeamPage::KickedOut;
    }
    else if (g_team_state.pending_join)
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

    close_join_request_modal();
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
    case sys::EventType::TeamAdvertise:
        handle_team_advertise(static_cast<sys::TeamAdvertiseEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamJoinRequest:
        handle_team_join_request(static_cast<sys::TeamJoinRequestEvent*>(event)->data);
        break;
    case sys::EventType::TeamJoinAccept:
        handle_team_join_accept(static_cast<sys::TeamJoinAcceptEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamJoinConfirm:
        handle_team_join_confirm(static_cast<sys::TeamJoinConfirmEvent*>(event)->data);
        changed = true;
        break;
    case sys::EventType::TeamJoinDecision:
        handle_team_join_decision(static_cast<sys::TeamJoinDecisionEvent*>(event)->data);
        changed = true;
        break;
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
    case sys::EventType::SystemTick:
    {
        process_keydist_retries();
        if (g_team_state.pending_join && g_team_state.pending_join_started_s != 0)
        {
            uint32_t now = now_secs();
            if ((now - g_team_state.pending_join_started_s) >= kJoinPendingTimeoutSec)
            {
                g_team_state.pending_join = false;
                g_team_state.pending_join_started_s = 0;
                g_team_state.has_join_target = false;
                g_team_state.page = TeamPage::JoinTeam;
                g_team_state.nav_stack.clear();
                ::ui::SystemNotification::show("Join timed out", 2000);
                changed = true;
            }
        }
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
