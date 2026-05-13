#pragma once

#include <stdint.h>

namespace chat::delivery
{

enum class DeliveryState : uint8_t
{
    Unknown,
    Queued,
    Sending,
    Sent,
    Delivered,
    Failed,
    Received,
};

enum class DeliveryFailureKind : uint8_t
{
    None,
    PeerKeyMissing,
    LocalIdentityMissing,
    RadioSendFailed,
    AckTimeout,
    UnsupportedProtocol,
    Rejected,
    Unknown,
};

struct ChatDeliveryRef
{
    uint64_t local_id = 0;
    uint32_t protocol_id = 0;
    uint32_t nonce_or_seq = 0;

    bool isValid() const
    {
        return local_id != 0 || protocol_id != 0 || nonce_or_seq != 0;
    }

    bool operator==(const ChatDeliveryRef& other) const
    {
        return local_id == other.local_id &&
               protocol_id == other.protocol_id &&
               nonce_or_seq == other.nonce_or_seq;
    }
};

struct ChatDeliveryRecord
{
    ChatDeliveryRef ref;
    DeliveryState state = DeliveryState::Unknown;
    DeliveryFailureKind failure = DeliveryFailureKind::None;
    uint32_t updated_at_ms = 0;
};

} // namespace chat::delivery
