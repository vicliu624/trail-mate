#pragma once
#include "geocaching/storage/logical_state.h"
#include "geocaching/storage/outgoing_record.h"
#include "geocaching/storage/tx_attempt.h"

namespace geocaching::storage
{
inline bool validateAttemptReferences(const LogicalState::View& state)
{
    size_t cursor = 0;
    MutationView entry;
    while (state.next(cursor, entry))
    {
        if (entry.table != 13) continue;
        TxAttemptView attempt;
        ByteView outgoing_bytes;
        OutgoingView outgoing;
        if (!decodeTxAttempt(entry.key, entry.value, attempt) || !state.find(5, attempt.request_key, outgoing_bytes) ||
            !decodeOutgoing(attempt.request_key, outgoing_bytes, outgoing)) return false;
    }
    return true;
}
} // namespace geocaching::storage
