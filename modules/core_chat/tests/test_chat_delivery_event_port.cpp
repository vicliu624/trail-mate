#include "chat/delivery/chat_delivery_event_port.h"

#include <cassert>

int main()
{
    chat::delivery::ChatDeliveryReadModel read_model;
    chat::delivery::ChatDeliveryEventProjector projector(read_model);
    chat::delivery::ProjectingChatDeliveryEventPort port(projector);

    chat::delivery::ChatDeliveryEvent sent{};
    sent.ref.protocol_id = 101;
    sent.state = chat::delivery::DeliveryState::Sent;
    sent.timestamp_ms = 1000;
    port.publishDeliveryEvent(sent);

    chat::delivery::ChatDeliveryRecord record{};
    assert(read_model.find(sent.ref, record));
    assert(record.state == chat::delivery::DeliveryState::Sent);
    assert(record.failure == chat::delivery::DeliveryFailureKind::None);
    assert(record.updated_at_ms == 1000);

    chat::delivery::ChatDeliveryEvent failed{};
    failed.ref.protocol_id = 102;
    failed.state = chat::delivery::DeliveryState::Failed;
    failed.failure = chat::delivery::SendFailureKind::PeerKeyMissing;
    failed.timestamp_ms = 2000;
    port.publishDeliveryEvent(failed);

    assert(read_model.find(failed.ref, record));
    assert(record.state == chat::delivery::DeliveryState::Failed);
    assert(record.failure == chat::delivery::DeliveryFailureKind::PeerKeyMissing);
    assert(record.updated_at_ms == 2000);

    chat::delivery::ChatDeliveryEvent invalid{};
    invalid.state = chat::delivery::DeliveryState::Delivered;
    port.publishDeliveryEvent(invalid);
    assert(read_model.size() == 2);

    return 0;
}
