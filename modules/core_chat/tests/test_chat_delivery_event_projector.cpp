#include "chat/delivery/chat_delivery_event_projector.h"

#include <cassert>

namespace
{

chat::delivery::ChatDeliveryRef ref(uint32_t id)
{
    chat::delivery::ChatDeliveryRef out{};
    out.protocol_id = id;
    return out;
}

} // namespace

int main()
{
    chat::delivery::ChatDeliveryReadModel read_model;
    chat::delivery::ChatDeliveryEventProjector projector(read_model);

    projector.onQueued(ref(1), 100);
    chat::delivery::ChatDeliveryRecord record{};
    assert(read_model.find(ref(1), record));
    assert(record.state == chat::delivery::DeliveryState::Queued);
    assert(record.failure == chat::delivery::DeliveryFailureKind::None);
    assert(record.updated_at_ms == 100);

    projector.onSending(ref(1), 150);
    assert(read_model.find(ref(1), record));
    assert(record.state == chat::delivery::DeliveryState::Sending);
    assert(record.failure == chat::delivery::DeliveryFailureKind::None);

    projector.onSent(ref(1), 200);
    assert(read_model.find(ref(1), record));
    assert(record.state == chat::delivery::DeliveryState::Sent);

    projector.onDelivered(ref(1), 250);
    assert(read_model.find(ref(1), record));
    assert(record.state == chat::delivery::DeliveryState::Delivered);

    projector.onFailed(ref(2),
                       chat::delivery::SendFailureKind::PeerKeyMissing,
                       300);
    assert(read_model.find(ref(2), record));
    assert(record.state == chat::delivery::DeliveryState::Failed);
    assert(record.failure ==
           chat::delivery::DeliveryFailureKind::PeerKeyMissing);

    projector.onFailed(ref(3),
                       chat::delivery::SendFailureKind::LocalIdentityMissing,
                       400);
    assert(read_model.find(ref(3), record));
    assert(record.failure ==
           chat::delivery::DeliveryFailureKind::LocalIdentityMissing);

    projector.onFailed(ref(4),
                       chat::delivery::SendFailureKind::RadioSendFailed,
                       500);
    assert(read_model.find(ref(4), record));
    assert(record.failure ==
           chat::delivery::DeliveryFailureKind::RadioSendFailed);

    projector.onFailed(ref(5),
                       chat::delivery::SendFailureKind::AckTimeout,
                       600);
    assert(read_model.find(ref(5), record));
    assert(record.failure == chat::delivery::DeliveryFailureKind::AckTimeout);

    projector.onReceived(ref(6), 700);
    assert(read_model.find(ref(6), record));
    assert(record.state == chat::delivery::DeliveryState::Received);
    assert(record.failure == chat::delivery::DeliveryFailureKind::None);

    return 0;
}
