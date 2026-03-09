#include "platform/esp/arduino_common/team/event/team_app_data_event_bus_bridge.h"

#include "sys/event_bus.h"

namespace team::infra
{

void TeamAppDataEventBusBridge::onUnhandledAppData(const chat::MeshIncomingData& msg)
{
    sys::EventBus::publish(
        new sys::AppDataEvent(
            msg.portnum,
            msg.from,
            msg.to,
            msg.packet_id,
            static_cast<uint8_t>(msg.channel),
            msg.channel_hash,
            msg.want_response,
            msg.payload,
            &msg.rx_meta),
        0);
}

} // namespace team::infra
