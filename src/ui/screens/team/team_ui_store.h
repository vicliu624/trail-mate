/**
 * @file team_ui_store.h
 * @brief Team UI snapshot store interface (stub-ready)
 */

#pragma once

#include "team_state.h"

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

} // namespace ui
} // namespace team
