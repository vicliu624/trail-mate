#include "platform/esp/arduino_common/team/event/team_event_bus_sink.h"

#include "team/protocol/team_position.h"
#include "sys/event_bus.h"

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
    team::proto::TeamPositionMessage msg;
    if (event.ctx.from != 0 &&
        team::proto::decodeTeamPositionMessage(event.payload.data(),
                                              event.payload.size(),
                                              &msg))
    {
        const uint32_t timestamp = (msg.ts != 0) ? msg.ts : event.ctx.timestamp;
        sys::EventBus::publish(
            new sys::NodePositionUpdateEvent(
                event.ctx.from,
                msg.lat_e7,
                msg.lon_e7,
                team::proto::teamPositionHasAltitude(msg),
                team::proto::teamPositionHasAltitude(msg) ? msg.alt_m : 0,
                timestamp,
                0,
                0,
                0,
                0,
                0),
            0);
    }

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
