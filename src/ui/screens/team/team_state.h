/**
 * @file team_state.h
 * @brief Team page UI state
 */

#pragma once

#include "lvgl.h"
#include "../../widgets/top_bar.h"
#include "../../../team/domain/team_types.h"
#include "../../../team/protocol/team_mgmt.h"
#include <string>
#include <vector>
#include <array>

namespace team
{
namespace ui
{

enum class TeamPage
{
    StatusNotInTeam,
    StatusInTeam,
    TeamHome,
    Invite,
    InviteNfc,
    JoinTeam,
    JoinNfc,
    EnterCode,
    JoinPending,
    Members,
    MemberDetail,
    KickConfirm,
    KickedOut
};

enum class TeamInviteMode
{
    Radio,
    Nfc
};

struct TeamMemberUi
{
    uint32_t node_id = 0;
    std::string name;
    bool online = false;
    bool leader = false;
    uint32_t last_seen_s = 0;
};

struct NearbyTeamUi
{
    TeamId team_id{};
    std::string name;
    uint8_t signal_bars = 0;
    uint32_t last_seen_s = 0;
    uint32_t join_hint = 0;
    bool has_join_hint = false;
};

struct TeamPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* page_obj = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* actions = nullptr;

    lv_obj_t* action_btns[3] = { nullptr, nullptr, nullptr };
    lv_obj_t* action_labels[3] = { nullptr, nullptr, nullptr };
    lv_obj_t* detail_label = nullptr;

    std::vector<lv_obj_t*> list_items;
    std::vector<lv_obj_t*> focusables;
    std::vector<TeamPage> nav_stack;
    lv_obj_t* default_focus = nullptr;

    ::ui::widgets::TopBar top_bar_widget;
    lv_group_t* group = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_group_t* prev_group = nullptr;
    lv_obj_t* join_request_modal = nullptr;

    TeamPage page = TeamPage::StatusNotInTeam;
    int selected_member_index = -1;
    TeamInviteMode invite_mode = TeamInviteMode::Radio;

    bool in_team = false;
    bool pending_join = false;
    bool kicked_out = false;
    bool self_is_leader = false;
    uint32_t last_event_seq = 0;

    TeamId team_id{};
    bool has_team_id = false;
    TeamId join_target_id{};
    bool has_join_target = false;

    std::string team_name;
    uint32_t security_round = 0;
    std::string invite_code;
    uint32_t invite_expires_s = 0;
    uint32_t last_update_s = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk{};
    bool has_team_psk = false;
    bool waiting_new_keys = false;
    uint32_t pending_join_started_s = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> nfc_next_psk{};
    bool has_nfc_next_psk = false;
    uint32_t nfc_next_key_id = 0;
    std::vector<uint8_t> nfc_payload;
    bool has_nfc_payload = false;
    bool nfc_share_active = false;
    bool nfc_scan_active = false;
    uint32_t nfc_scan_started_s = 0;
    lv_obj_t* invite_code_textarea = nullptr;

    std::vector<TeamMemberUi> members;
    std::vector<NearbyTeamUi> nearby_teams;

    uint32_t pending_join_node_id = 0;
    std::string pending_join_name;
};

extern TeamPageState g_team_state;

} // namespace ui
} // namespace team
