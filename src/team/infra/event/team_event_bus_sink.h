#pragma once

#include "../../ports/i_team_event_sink.h"

namespace team::infra
{

class TeamEventBusSink : public team::ITeamEventSink
{
  public:
    void onTeamAdvertise(const TeamAdvertiseEvent& event) override;
    void onTeamJoinRequest(const TeamJoinRequestEvent& event) override;
    void onTeamJoinAccept(const TeamJoinAcceptEvent& event) override;
    void onTeamJoinConfirm(const TeamJoinConfirmEvent& event) override;
    void onTeamStatus(const TeamStatusEvent& event) override;
    void onTeamPosition(const TeamPositionEvent& event) override;
    void onTeamWaypoint(const TeamWaypointEvent& event) override;
    void onTeamError(const TeamErrorEvent& event) override;
};

} // namespace team::infra
