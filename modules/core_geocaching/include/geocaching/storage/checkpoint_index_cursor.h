#pragma once
#include "geocaching/storage/checkpoint.h"
#include "geocaching/storage/transaction_index_cursor.h"

namespace geocaching::storage
{
// The owner must first verify checkpoint counts/digest and pin its slot against
// replacement. This cursor validates one page and derives locations, not a root.
class CheckpointIndexCursor
{
  public:
    bool open(ByteView frame, uint64_t checkpoint_sequence, char slot, uint32_t offset)
    {
        *this = {};
        RecordFrameView decoded;
        if ((slot != 'a' && slot != 'b') || !checkpoint_sequence || frame.size > UINT32_MAX - offset ||
            !decodeRecordFrame(frame, decoded) || decoded.kind != RecordKind::CheckpointPage || decoded.sequence != checkpoint_sequence) return false;
        protocol::CmpReader header(decoded.payload);
        size_t fields = 0;
        uint64_t index = 0;
        if (!header.array(fields, 2) || fields != 2 || !header.unsignedInteger(index) || !page_.open(decoded.payload, index)) return false;
        frame_ = frame;
        base_ = {checkpoint_sequence, checkpoint_sequence, offset, 0, 0,
                 slot == 'a' ? IndexedValueSource::CheckpointA : IndexedValueSource::CheckpointB};
        return true;
    }
    bool complete() const { return page_.complete(); }
    bool next(IndexedMutation& out)
    {
        out = {};
        MutationView row;
        if (!page_.next(row)) return false;
        out.table = row.table;
        out.key = row.key;
        out.location = base_;
        out.location.value_offset += base_.frame_offset + static_cast<uint32_t>(row.value.data - frame_.data);
        out.location.value_size = static_cast<uint32_t>(row.value.size);
        return true;
    }

  private:
    CheckpointPageCursor page_;
    ByteView frame_;
    JournalValueLocation base_;
};
static_assert(sizeof(CheckpointIndexCursor) <= 128, "Checkpoint indexing must not retain a page array");
} // namespace geocaching::storage
