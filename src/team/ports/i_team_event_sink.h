#pragma once

#include "../domain/team_events.h"

namespace team
{

class ITeamEventSink
{
  public:
    virtual ~ITeamEventSink() = default;

    virtual void onTeamAdvertise(const TeamAdvertiseEvent& event) = 0;
    virtual void onTeamJoinRequest(const TeamJoinRequestEvent& event) = 0;
    virtual void onTeamJoinAccept(const TeamJoinAcceptEvent& event) = 0;
    virtual void onTeamJoinConfirm(const TeamJoinConfirmEvent& event) = 0;
    virtual void onTeamJoinDecision(const TeamJoinDecisionEvent& event) = 0;
    virtual void onTeamKick(const TeamKickEvent& event) = 0;
    virtual void onTeamTransferLeader(const TeamTransferLeaderEvent& event) = 0;
    virtual void onTeamKeyDist(const TeamKeyDistEvent& event) = 0;
    virtual void onTeamStatus(const TeamStatusEvent& event) = 0;
    virtual void onTeamPosition(const TeamPositionEvent& event) = 0;
    virtual void onTeamWaypoint(const TeamWaypointEvent& event) = 0;
    virtual void onTeamError(const TeamErrorEvent& event) = 0;
};

} // namespace team
