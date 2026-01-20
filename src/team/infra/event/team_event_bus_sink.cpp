#include "team_event_bus_sink.h"

#include "../../../sys/event_bus.h"

namespace team::infra
{

void TeamEventBusSink::onTeamAdvertise(const TeamAdvertiseEvent& event)
{
    sys::EventBus::publish(new sys::TeamAdvertiseEvent(event), 0);
}

void TeamEventBusSink::onTeamJoinRequest(const TeamJoinRequestEvent& event)
{
    sys::EventBus::publish(new sys::TeamJoinRequestEvent(event), 0);
}

void TeamEventBusSink::onTeamJoinAccept(const TeamJoinAcceptEvent& event)
{
    sys::EventBus::publish(new sys::TeamJoinAcceptEvent(event), 0);
}

void TeamEventBusSink::onTeamJoinConfirm(const TeamJoinConfirmEvent& event)
{
    sys::EventBus::publish(new sys::TeamJoinConfirmEvent(event), 0);
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

void TeamEventBusSink::onTeamError(const TeamErrorEvent& event)
{
    sys::EventBus::publish(new sys::TeamErrorEvent(event), 0);
}

} // namespace team::infra
