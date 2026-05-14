#include "chat/delivery/legacy_chat_send_result_mapper.h"

#include <cassert>

int main()
{
    using namespace chat::delivery;

    assert(mapLegacyChatSendFailure(LegacyChatSendFailure::PeerKeyMissing) ==
           SendFailureKind::PeerKeyMissing);
    assert(mapLegacyChatSendFailure(LegacyChatSendFailure::LocalIdentityMissing) ==
           SendFailureKind::LocalIdentityMissing);
    assert(mapLegacyChatSendFailure(LegacyChatSendFailure::RadioSendFailed) ==
           SendFailureKind::RadioSendFailed);
    assert(mapLegacyChatSendFailure(LegacyChatSendFailure::UnsupportedProtocol) ==
           SendFailureKind::UnsupportedProtocol);
    assert(mapLegacyChatSendFailure(LegacyChatSendFailure::Rejected) ==
           SendFailureKind::Rejected);

    LegacyChatSendResult sent{};
    sent.ref.protocol_id = 10;
    sent.success = true;
    sent.failure = LegacyChatSendFailure::PeerKeyMissing;
    sent.timestamp_ms = 111;
    ChatDeliveryEvent event = mapLegacyChatSendResult(sent);
    assert(event.ref.protocol_id == 10);
    assert(event.state == DeliveryState::Sent);
    assert(event.failure == SendFailureKind::None);
    assert(event.timestamp_ms == 111);

    LegacyChatSendResult failed{};
    failed.ref.protocol_id = 11;
    failed.success = false;
    failed.failure = LegacyChatSendFailure::None;
    failed.timestamp_ms = 222;
    event = mapLegacyChatSendResult(failed);
    assert(event.state == DeliveryState::Failed);
    assert(event.failure == SendFailureKind::Unknown);
    assert(event.timestamp_ms == 222);

    failed.failure = LegacyChatSendFailure::RadioSendFailed;
    event = mapLegacyChatSendResult(failed);
    assert(event.state == DeliveryState::Failed);
    assert(event.failure == SendFailureKind::RadioSendFailed);

    event = makeAckTimeoutDeliveryEvent(ChatDeliveryRef{0, 12, 0}, 333);
    assert(event.ref.protocol_id == 12);
    assert(event.state == DeliveryState::Failed);
    assert(event.failure == SendFailureKind::AckTimeout);
    assert(event.timestamp_ms == 333);

    return 0;
}
