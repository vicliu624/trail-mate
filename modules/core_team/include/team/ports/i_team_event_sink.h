#pragma once

#include "../domain/team_events.h"

namespace team
{

class ITeamEventSink
{
  public:
    virtual ~ITeamEventSink() = default;

    virtual void onTeamKick(const TeamKickEvent& event) = 0;
    virtual void onTeamTransferLeader(const TeamTransferLeaderEvent& event) = 0;
    virtual void onTeamKeyDist(const TeamKeyDistEvent& event) = 0;
    virtual void onTeamStatus(const TeamStatusEvent& event) = 0;
    virtual void onTeamPosition(const TeamPositionEvent& event) = 0;
    virtual void onTeamWaypoint(const TeamWaypointEvent& event) = 0;
    virtual void onTeamTrack(const TeamTrackEvent& event) = 0;
    virtual void onTeamChat(const TeamChatEvent& event) = 0;
    virtual void onTeamError(const TeamErrorEvent& event) = 0;
};

} // namespace team
