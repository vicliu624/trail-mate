/**
 * @file team_state.h
 * @brief Team page UI state
 */

#pragma once

#include "../../../team/domain/team_types.h"
#include "../../../team/protocol/team_mgmt.h"
#include "../../widgets/top_bar.h"
#include "lvgl.h"
#include <array>
#include <string>
#include <vector>

namespace team
{
namespace ui
{

constexpr size_t kTeamMaxMembers = 4;
constexpr uint8_t kTeamColorUnassigned = 0xFF;
static constexpr uint32_t kTeamMemberColors[kTeamMaxMembers] = {
    0xFF3B30, // red
    0x34C759, // green
    0x007AFF, // blue
    0xFFCC00  // yellow
};

inline uint32_t team_color_from_index(uint8_t index)
{
    if (index >= kTeamMaxMembers)
    {
        return kTeamMemberColors[0];
    }
    return kTeamMemberColors[index];
}

inline uint8_t team_color_index_from_node_id(uint32_t node_id)
{
    uint32_t h = node_id;
    h ^= h >> 16;
    h *= 0x7feb352d;
    h ^= h >> 15;
    h *= 0x846ca68b;
    h ^= h >> 16;
    return static_cast<uint8_t>(h % kTeamMaxMembers);
}

enum class TeamPage
{
    StatusNotInTeam,
    StatusInTeam,
    TeamHome,
    JoinPending,
    Members,
    MemberDetail,
    KickConfirm,
    KickedOut
};

struct TeamMemberUi
{
    uint32_t node_id = 0;
    std::string name;
    bool online = false;
    bool leader = false;
    uint32_t last_seen_s = 0;
    uint8_t color_index = kTeamColorUnassigned;
};

struct TeamPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* page_obj = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* body = nullptr;
    lv_obj_t* actions = nullptr;

    lv_obj_t* action_btns[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* action_labels[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* detail_label = nullptr;

    std::vector<lv_obj_t*> list_items;
    std::vector<lv_obj_t*> focusables;
    std::vector<TeamPage> nav_stack;
    lv_obj_t* default_focus = nullptr;

    ::ui::widgets::TopBar top_bar_widget;
    lv_group_t* group = nullptr;
    lv_group_t* modal_group = nullptr;
    lv_group_t* prev_group = nullptr;
    lv_obj_t* leave_confirm_modal = nullptr;

    TeamPage page = TeamPage::StatusNotInTeam;
    int selected_member_index = -1;

    bool in_team = false;
    bool pending_join = false;
    bool kicked_out = false;
    bool self_is_leader = false;
    uint32_t last_event_seq = 0;
    TeamPairingRole pairing_role = TeamPairingRole::None;
    TeamPairingState pairing_state = TeamPairingState::Idle;
    uint32_t pairing_peer_id = 0;
    std::string pairing_team_name;

    TeamId team_id{};
    bool has_team_id = false;

    std::string team_name;
    uint32_t security_round = 0;
    uint32_t last_update_s = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk{};
    bool has_team_psk = false;
    bool waiting_new_keys = false;
    uint32_t pending_join_started_s = 0;
    uint32_t team_chat_unread = 0;

    std::vector<TeamMemberUi> members;
};

extern TeamPageState g_team_state;

} // namespace ui
} // namespace team
