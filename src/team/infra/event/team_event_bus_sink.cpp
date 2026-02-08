#include "team_event_bus_sink.h"

#include "../../../sys/event_bus.h"

namespace team::infra
{

void TeamEventBusSink::onTeamKick(const TeamKickEvent& event)
{
    sys::EventBus::publish(new sys::TeamKickEvent(event), 0);
}

void TeamEventBusSink::onTeamTransferLeader(const TeamTransferLeaderEvent& event)
{
    sys::EventBus::publish(new sys::TeamTransferLeaderEvent(event), 0);
}

void TeamEventBusSink::onTeamKeyDist(const TeamKeyDistEvent& event)
{
    sys::EventBus::publish(new sys::TeamKeyDistEvent(event), 0);
}

void TeamEventBusSink::onTeamStatus(const TeamStatusEvent& event)
{
    sys::EventBus::publish(new sys::TeamStatusEvent(event), 0);
}

void TeamEventBusSink::onTeamPosition(const TeamPositionEvent& event)
{
    sys::EventBus::publish(new sys::TeamPositionEvent(event), 0);
}

void TeamEventBusSink::onTeamWaypoint(const TeamWaypointEvent& event)
{
    sys::EventBus::publish(new sys::TeamWaypointEvent(event), 0);
}

void TeamEventBusSink::onTeamTrack(const TeamTrackEvent& event)
{
    sys::EventBus::publish(new sys::TeamTrackEvent(event), 0);
}

void TeamEventBusSink::onTeamChat(const TeamChatEvent& event)
{
    sys::EventBus::publish(new sys::TeamChatEvent(event), 0);
}

void TeamEventBusSink::onTeamError(const TeamErrorEvent& event)
{
    sys::EventBus::publish(new sys::TeamErrorEvent(event), 0);
}

} // namespace team::infra
