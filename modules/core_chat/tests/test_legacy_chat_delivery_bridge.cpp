#include "chat/delivery/legacy_chat_delivery_bridge.h"

#include <cassert>

namespace
{

chat::ChatMessage message(chat::MessageStatus status, chat::MessageId id)
{
    chat::ChatMessage out;
    out.status = status;
    out.msg_id = id;
    return out;
}

} // namespace

int main()
{
    const auto queued =
        chat::delivery::toDeliveryRecord(message(chat::MessageStatus::Queued, 10),
                                         100);
    assert(queued.ref.protocol_id == 10);
    assert(queued.state == chat::delivery::DeliveryState::Queued);
    assert(queued.failure == chat::delivery::DeliveryFailureKind::None);
    assert(queued.updated_at_ms == 100);

    const auto sent =
        chat::delivery::toDeliveryRecord(message(chat::MessageStatus::Sent, 11));
    assert(sent.state == chat::delivery::DeliveryState::Sent);
    assert(sent.failure == chat::delivery::DeliveryFailureKind::None);

    const auto failed =
        chat::delivery::toDeliveryRecord(message(chat::MessageStatus::Failed, 12));
    assert(failed.state == chat::delivery::DeliveryState::Failed);
    assert(failed.failure == chat::delivery::DeliveryFailureKind::Unknown);

    const auto incoming = chat::delivery::toDeliveryRecord(
        message(chat::MessageStatus::Incoming, 13));
    assert(incoming.state == chat::delivery::DeliveryState::Received);

    chat::delivery::ChatDeliveryRef ref{};
    ref.protocol_id = 99;
    const auto key_missing = chat::delivery::toFailedDeliveryRecord(
        ref,
        chat::delivery::SendFailureKind::PeerKeyMissing,
        500);
    assert(key_missing.ref.protocol_id == 99);
    assert(key_missing.state == chat::delivery::DeliveryState::Failed);
    assert(key_missing.failure ==
           chat::delivery::DeliveryFailureKind::PeerKeyMissing);
    assert(key_missing.updated_at_ms == 500);

    return 0;
}
