#pragma once

#include "platform/ui/team_ui_snapshot_store.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace team
{
namespace ui
{

struct TeamPageColorContext
{
    constexpr TeamPageColorContext() = default;
    explicit constexpr TeamPageColorContext(uint32_t self_node_id_in)
        : self_node_id(self_node_id_in)
    {
    }

    uint32_t self_node_id = 0;
};

struct TeamPagePersistentState
{
    bool in_team = false;
    bool pending_join = false;
    uint32_t pending_join_started_s = 0;
    bool kicked_out = false;
    bool self_is_leader = false;
    uint32_t last_event_seq = 0;
    uint32_t team_chat_unread = 0;

    TeamId team_id{};
    bool has_team_id = false;

    std::string team_name;
    uint32_t security_round = 0;
    uint32_t last_update_s = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk{};
    bool has_team_psk = false;

    std::vector<TeamMemberUi> members;
};

class TeamPageStateStore
{
  public:
    bool loadOnce(ITeamUiSnapshotStore& store,
                  TeamPagePersistentState& state,
                  TeamPageColorContext color_context);
    bool refresh(ITeamUiSnapshotStore& store,
                 TeamPagePersistentState& state,
                 TeamPageColorContext color_context);
    void save(ITeamUiSnapshotStore& store,
              const TeamPagePersistentState& state);
    void resetLoaded();

    static TeamUiSnapshot snapshotFromState(const TeamPagePersistentState& state);
    static void applySnapshot(TeamPagePersistentState& state,
                              const TeamUiSnapshot& snapshot,
                              TeamPageColorContext color_context);

  private:
    bool loaded_ = false;
};

void assignTeamPageMemberColors(std::vector<TeamMemberUi>& members,
                                TeamPageColorContext color_context);

} // namespace ui
} // namespace team
