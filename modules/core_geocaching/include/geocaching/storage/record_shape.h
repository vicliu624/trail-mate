#pragma once
#include "geocaching/storage/author_issued.h"
#include "geocaching/storage/cache_head.h"
#include "geocaching/storage/draft_record.h"
#include "geocaching/storage/install_record.h"
#include "geocaching/storage/object_ref.h"
#include "geocaching/storage/outgoing_record.h"
#include "geocaching/storage/summary_cache.h"
#include "geocaching/storage/task_record.h"
#include "geocaching/storage/transaction.h"
#include "geocaching/storage/tx_attempt.h"

namespace geocaching::storage
{
// Encoding checks only. Signatures, transport origin, immutable-author history
// and cross-row references must still be checked by the business transaction.
inline bool validStoredRowShape(const MutationView& row)
{
    constexpr uint8_t key_sizes[] = {0, 32, 32, 36, 16, 48, 48, 32, 16, 48, 16, 32, 16, 64};
    if (row.table < 1 || row.table > 13 || !row.key.data || row.key.size != key_sizes[row.table]) return false;
    if (row.erase) return !row.value.size;
    if (!row.value.data || row.value.size > 32768) return false;
    switch (row.table)
    {
    case 1:
    {
        ObjectRefView value;
        return decodeObjectRef(row.key, row.value, value);
    }
    case 2:
    {
        CacheHeadView value;
        return decodeCacheHead(row.key, row.value, value);
    }
    case 3:
    {
        AuthorIssuedView value;
        return decodeAuthorIssued(row.key, row.value, value);
    }
    case 4:
    {
        DraftView value;
        return decodeDraft(row.key, row.value, value);
    }
    case 5:
    {
        OutgoingView value;
        return decodeOutgoing(row.key, row.value, value);
    }
    case 9:
    {
        SummaryCacheView value;
        return decodeSummaryCache(row.key, row.value, value);
    }
    case 10:
    {
        TaskView value;
        return decodeTask(row.key, row.value, value);
    }
    case 12:
    {
        InstallRecordView value;
        return decodeInstallRecord(row.key, row.value, value);
    }
    case 13:
    {
        TxAttemptView value;
        return decodeTxAttempt(row.key, row.value, value);
    }
    default:
        break;
    }
    protocol::CmpReader reader(row.value);
    size_t count = 0;
    uint64_t number = 0;
    ByteView bytes;
    StoredTime time;
    const auto binary = [&](size_t size)
    { return reader.binary(bytes, size) && bytes.size == size; };
    const auto optional_time = [&](bool deadline)
    {
        auto probe = reader;
        if (probe.nil())
        {
            reader = probe;
            return true;
        }
        return decodeStoredTime(reader, time) && (!deadline || (time.has_utc && time.utc_trusted));
    };
    if (row.table == 6)
    {
        return reader.array(count, 5) && count == 5 && reader.unsignedInteger(number) && number == 1 && binary(32) &&
               reader.binary(bytes, 8192) && bytes.size && decodeStoredTime(reader, time) && optional_time(true) && reader.finished();
    }
    if (row.table == 7)
    {
        if (!reader.array(count, 4) || count != 4 || !reader.unsignedInteger(number) || number > 2) return false;
        auto probe = reader;
        if (probe.nil()) reader = probe;
        else if (!binary(32)) return false;
        std::string_view note;
        return reader.text(note, 2048) && protocol::validRecordText(note, true, false) && decodeStoredTime(reader, time) && reader.finished();
    }
    if (row.table == 8)
    {
        return reader.array(count, 8) && count == 8 && binary(16) && binary(64) && binary(16) &&
               reader.unsignedInteger(number) && reader.unsignedInteger(number) && number <= 5 &&
               decodeStoredTime(reader, time) && optional_time(false) && optional_time(true) && reader.finished();
    }
    if (!reader.array(count, 2) || count != 2) return false;
    for (unsigned field = 0; field < 2; ++field)
    {
        if (!reader.array(count, 2) || (field == 0 && count == 1)) return false;
        ByteView previous;
        for (size_t i = 0; i < count; ++i)
        {
            if (!binary(32) || (i && !std::memcmp(previous.data, bytes.data, 32))) return false;
            previous = bytes;
        }
    }
    return reader.finished();
}
inline bool validTransactionRowShapes(ByteView payload, uint64_t previous_sequence)
{
    if (!validateTransaction(payload, previous_sequence)) return false;
    protocol::CmpReader reader(payload);
    size_t count = 0;
    if (!readTransactionHeader(reader, previous_sequence, count)) return false;
    for (size_t i = 0; i < count; ++i)
    {
        MutationView row;
        if (!readTransactionMutation(reader, row) || !validStoredRowShape(row)) return false;
    }
    return reader.finished();
}
} // namespace geocaching::storage
