#pragma once
#include "geocaching/storage/stored_time.h"

namespace geocaching::storage
{
enum class TxAttemptState : uint8_t
{
    Accepted,
    InFlight,
    Delivered,
    Failed,
    CancelledBeforeSend
};
struct TxAttemptView
{
    ByteView request_key;
    ByteView attempt_id;
    ByteView lxmf_hash;
    TxAttemptState state = TxAttemptState::Accepted;
    StoredTime submitted;
    StoredTime finished;
    bool has_finished = false;
};
enum class AttemptTransition : uint8_t
{
    Rejected,
    Unchanged,
    Changed
};
// Call only with a decoded attempt. Hash bytes remain borrowed until encoding.
inline AttemptTransition recordAttemptTransportHash(TxAttemptView& attempt, ByteView hash)
{
    if (!hash.data || hash.size != 32 || (attempt.lxmf_hash.size && (attempt.lxmf_hash.size != hash.size || std::memcmp(attempt.lxmf_hash.data, hash.data, hash.size)))) return AttemptTransition::Rejected;
    if (attempt.state == TxAttemptState::InFlight && attempt.lxmf_hash.size == 32) return AttemptTransition::Unchanged;
    if (attempt.state != TxAttemptState::Accepted) return AttemptTransition::Rejected;
    attempt.lxmf_hash = hash;
    attempt.state = TxAttemptState::InFlight;
    return AttemptTransition::Changed;
}
inline AttemptTransition finishAttemptTransport(TxAttemptView& attempt, TxAttemptState terminal, const StoredTime& finished)
{
    if (terminal != TxAttemptState::Delivered && terminal != TxAttemptState::Failed && terminal != TxAttemptState::CancelledBeforeSend)
        return AttemptTransition::Rejected;
    if (attempt.has_finished) return attempt.state == terminal ? AttemptTransition::Unchanged : AttemptTransition::Rejected;
    if ((terminal == TxAttemptState::Delivered && attempt.lxmf_hash.size != 32) ||
        (terminal == TxAttemptState::CancelledBeforeSend && (attempt.state != TxAttemptState::Accepted || attempt.lxmf_hash.size)))
        return AttemptTransition::Rejected;
    attempt.state = terminal;
    attempt.finished = finished;
    attempt.has_finished = true;
    return AttemptTransition::Changed;
}
inline bool decodeTxAttempt(ByteView key, ByteView value, TxAttemptView& out)
{
    out = {};
    if (!key.data || key.size != 64 || !value.data || value.size > 32768) return false;
    protocol::CmpReader reader(value);
    size_t fields = 0;
    uint64_t state = 0;
    TxAttemptView candidate;
    candidate.request_key = {key.data, 48};
    candidate.attempt_id = {key.data + 48, 16};
    if (!reader.array(fields, 4) || fields != 4) return false;
    auto nullable = reader;
    if (nullable.nil()) reader = nullable;
    else if (!reader.binary(candidate.lxmf_hash, 32) || candidate.lxmf_hash.size != 32) return false;
    if (!reader.unsignedInteger(state) || state > 4 || !decodeStoredTime(reader, candidate.submitted)) return false;
    nullable = reader;
    if (nullable.nil()) reader = nullable;
    else
    {
        if (!decodeStoredTime(reader, candidate.finished)) return false;
        candidate.has_finished = true;
    }
    if (!reader.finished() || (state >= 2) != candidate.has_finished) return false;
    candidate.state = static_cast<TxAttemptState>(state);
    out = candidate;
    return true;
}
inline bool encodeTxAttempt(ByteView key, const TxAttemptView& value, uint8_t* output, size_t capacity, size_t& written)
{
    written = 0;
    protocol::CmpWriter writer(output, capacity);
    if (!writer.array(4) || !(value.lxmf_hash.size ? writer.binary(value.lxmf_hash) : writer.nil()) ||
        !writer.unsignedInteger(static_cast<uint8_t>(value.state)) || !encodeStoredTime(writer, value.submitted) ||
        !(value.has_finished ? encodeStoredTime(writer, value.finished) : writer.nil())) return false;
    TxAttemptView checked;
    if (!decodeTxAttempt(key, {output, writer.size()}, checked)) return false;
    written = writer.size();
    return true;
}
} // namespace geocaching::storage
