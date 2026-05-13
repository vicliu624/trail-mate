#pragma once

#include <stdint.h>

namespace ui::chat
{

enum class MessageOrigin : uint8_t
{
    LocalDraft,
    LocalPending,
    LocalStored,
    RemoteStored,
    SystemGenerated,
};

struct MessageRef
{
    MessageOrigin origin = MessageOrigin::SystemGenerated;

    uint64_t local_id = 0;
    uint32_t protocol_id = 0;
    uint32_t nonce_or_seq = 0;

    bool isValid() const
    {
        return local_id != 0 || protocol_id != 0 || nonce_or_seq != 0;
    }
};

enum class MessageDeliveryState : uint8_t
{
    Draft,
    Queued,
    Sending,
    Sent,
    Delivered,
    Failed,
    Received,
    Unknown,
};

enum class MessageFailureKind : uint8_t
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

} // namespace ui::chat
