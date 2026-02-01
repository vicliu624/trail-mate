/**
 * @file team_ui_store.h
 * @brief Team UI snapshot store interface (stub-ready)
 */

#pragma once

#include "team_state.h"
#include "../../../team/protocol/team_chat.h"
#include <cstddef>

namespace team
{
namespace ui
{

struct TeamUiSnapshot
{
    bool in_team = false;
    bool pending_join = false;
    uint32_t pending_join_started_s = 0;
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

    std::vector<TeamMemberUi> members;
    std::vector<NearbyTeamUi> nearby_teams;
};

enum class TeamKeyEventType : uint8_t
{
    TeamCreated = 1,
    MemberAccepted = 2,
    MemberKicked = 3,
    LeaderTransferred = 4,
    EpochRotated = 5
};

struct TeamChatLogEntry
{
    bool incoming = false;
    uint32_t ts = 0;
    uint32_t peer_id = 0;
    team::proto::TeamChatType type = team::proto::TeamChatType::Text;
    std::vector<uint8_t> payload;
};

struct TeamPosSample
{
    uint32_t member_id = 0;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    int16_t alt_m = 0;
    uint16_t speed_dmps = 0;
    uint32_t ts = 0;
};

class ITeamUiStore
{
  public:
    virtual ~ITeamUiStore() = default;
    virtual bool load(TeamUiSnapshot& out) = 0;
    virtual void save(const TeamUiSnapshot& in) = 0;
    virtual void clear() = 0;
};

// Simple in-memory stub store (acts as fake persistence until real store is wired).
class TeamUiStoreStub : public ITeamUiStore
{
  public:
    bool load(TeamUiSnapshot& out) override;
    void save(const TeamUiSnapshot& in) override;
    void clear() override;

  private:
    static bool has_snapshot_;
    static TeamUiSnapshot snapshot_;
};

ITeamUiStore& team_ui_get_store();
void team_ui_set_store(ITeamUiStore* store);
bool team_ui_append_key_event(const TeamId& team_id,
                              TeamKeyEventType type,
                              uint32_t event_seq,
                              uint32_t ts,
                              const uint8_t* payload,
                              size_t len);
bool team_ui_posring_append(const TeamId& team_id,
                            uint32_t member_id,
                            int32_t lat_e7,
                            int32_t lon_e7,
                            int16_t alt_m,
                            uint16_t speed_dmps,
                            uint32_t ts);
bool team_ui_posring_load_latest(const TeamId& team_id,
                                 std::vector<TeamPosSample>& out);
bool team_ui_chatlog_append(const TeamId& team_id,
                            uint32_t peer_id,
                            bool incoming,
                            uint32_t ts,
                            const std::string& text);
bool team_ui_chatlog_append_structured(const TeamId& team_id,
                                       uint32_t peer_id,
                                       bool incoming,
                                       uint32_t ts,
                                       team::proto::TeamChatType type,
                                       const std::vector<uint8_t>& payload);
bool team_ui_chatlog_load_recent(const TeamId& team_id,
                                 size_t max_count,
                                 std::vector<TeamChatLogEntry>& out);
bool team_ui_save_keys_now(const TeamId& team_id,
                           uint32_t key_id,
                           const std::array<uint8_t, team::proto::kTeamChannelPskSize>& psk);

} // namespace ui
} // namespace team
