#pragma once

#include "../../ports/i_team_event_sink.h"

namespace team::infra
{

class TeamEventBusSink : public team::ITeamEventSink
{
  public:
    void onTeamKick(const TeamKickEvent& event) override;
    void onTeamTransferLeader(const TeamTransferLeaderEvent& event) override;
    void onTeamKeyDist(const TeamKeyDistEvent& event) override;
    void onTeamStatus(const TeamStatusEvent& event) override;
    void onTeamPosition(const TeamPositionEvent& event) override;
    void onTeamWaypoint(const TeamWaypointEvent& event) override;
    void onTeamTrack(const TeamTrackEvent& event) override;
    void onTeamChat(const TeamChatEvent& event) override;
    void onTeamError(const TeamErrorEvent& event) override;
};

} // namespace team::infra
