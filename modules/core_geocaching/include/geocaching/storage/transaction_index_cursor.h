#pragma once
#include "geocaching/storage/record_frame.h"
#include "geocaching/storage/transaction.h"

namespace geocaching::storage
{
enum class IndexedValueSource : uint8_t
{
    Journal = 0,
    CheckpointA = 1,
    CheckpointB = 2
};
// Derived locations only; the GCR1 journal remains authoritative. An index
// consumer must bind its index to the volume and recheck the referenced frame,
// key/table and value span. Schema/reference validation must finish before
// derived entries become visible; a cursor does not commit index mutations.
struct JournalValueLocation
{
    uint64_t segment_first_sequence = 0, record_sequence = 0;
    uint32_t frame_offset = 0, value_offset = 0, value_size = 0;
    IndexedValueSource source = IndexedValueSource::Journal;
};
struct IndexedMutation
{
    uint8_t table = 0;
    ByteView key;
    JournalValueLocation location;
    bool erase = false;
};

// Borrows one caller-owned complete frame, never copies its values. Keys remain
// borrowed until the caller consumes each descriptor. This is a locator, not an
// SD reader: incremental frame I/O and index persistence belong to the owner.
class TransactionIndexCursor
{
  public:
    bool open(ByteView frame, uint64_t expected_previous, uint64_t segment_first_sequence, uint32_t frame_offset)
    {
        *this = {};
        RecordFrameView decoded;
        if (expected_previous == UINT64_MAX || !segment_first_sequence || frame.size > UINT32_MAX - frame_offset ||
            !decodeRecordFrame(frame, decoded) || decoded.kind != RecordKind::Transaction ||
            decoded.sequence != expected_previous + 1 || segment_first_sequence > decoded.sequence ||
            !validateTransaction(decoded.payload, expected_previous)) return false;
        reader_ = protocol::CmpReader(decoded.payload);
        if (!readTransactionHeader(reader_, expected_previous, remaining_)) return false;
        frame_ = frame;
        base_ = {segment_first_sequence, decoded.sequence, frame_offset, 0, 0};
        opened_ = true;
        return true;
    }
    bool complete() const { return opened_ && !failed_ && !remaining_; }
    bool next(IndexedMutation& out)
    {
        out = {};
        if (!remaining_) return false;
        MutationView mutation;
        if (!readTransactionMutation(reader_, mutation))
        {
            remaining_ = 0;
            failed_ = true;
            return false;
        }
        --remaining_;
        out.table = mutation.table;
        out.key = mutation.key;
        out.erase = mutation.erase;
        out.location = base_;
        if (!mutation.erase)
        {
            out.location.value_offset = base_.frame_offset + static_cast<uint32_t>(mutation.value.data - frame_.data);
            out.location.value_size = static_cast<uint32_t>(mutation.value.size);
        }
        return true;
    }

  private:
    protocol::CmpReader reader_{{}};
    ByteView frame_;
    JournalValueLocation base_;
    size_t remaining_ = 0;
    bool opened_ = false, failed_ = false;
};
static_assert(sizeof(TransactionIndexCursor) <= 96, "Index cursors must not retain a payload or key array");
} // namespace geocaching::storage
