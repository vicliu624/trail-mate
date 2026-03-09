#include "platform/esp/arduino_common/team/event/team_pairing_event_bus_sink.h"

#include "sys/event_bus.h"

namespace team::infra
{

void TeamPairingEventBusSink::onTeamPairingStateChanged(const TeamPairingEvent& event)
{
    sys::EventBus::publish(new sys::TeamPairingEvent(event), 0);
}

void TeamPairingEventBusSink::onTeamPairingKeyDist(const TeamKeyDistEvent& event)
{
    sys::EventBus::publish(new sys::TeamKeyDistEvent(event), 0);
}

} // namespace team::infra
