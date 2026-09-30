#pragma once
#include "geocaching/storage/transaction.h"

namespace geocaching::storage
{
inline bool checkpointKeyBefore(const MutationView& left, const MutationView& right)
{
    if (left.table != right.table) return left.table < right.table;
    const auto common = left.key.size < right.key.size ? left.key.size : right.key.size;
    const int comparison = common ? std::memcmp(left.key.data, right.key.data, common) : 0;
    return comparison < 0 || (comparison == 0 && left.key.size < right.key.size);
}

inline bool readCheckpointEntry(protocol::CmpReader& reader, MutationView& entry)
{
    entry = {};
    size_t fields = 0;
    uint64_t table = 0;
    if (!reader.array(fields, 3) || fields != 3 || !reader.unsignedInteger(table) || table < 1 || table > 13 ||
        !reader.binary(entry.key, 96) || !entry.key.size || !reader.binary(entry.value, 32768)) return false;
    entry.table = static_cast<uint8_t>(table);
    return true;
}

// Borrows a validated page, with no entry array. Open checks the complete page
// and key ordering before exposing any row; caller keeps its bytes immutable.
class CheckpointPageCursor
{
  public:
    bool open(ByteView payload, uint64_t expected_index, const MutationView* previous = nullptr)
    {
        *this = {};
        if (!payload.data || payload.size > 65536 ||
            (previous && (!previous->key.data || !previous->key.size || previous->key.size > 96))) return false;
        protocol::CmpReader reader(payload);
        size_t fields = 0;
        uint64_t index = 0;
        if (!reader.array(fields, 2) || fields != 2 || !reader.unsignedInteger(index) || index != expected_index ||
            !reader.array(count_, 65536)) return false;
        reader_ = reader;
        MutationView last = previous ? *previous : MutationView{};
        for (size_t i = 0; i < count_; ++i)
        {
            MutationView entry;
            if (!readCheckpointEntry(reader, entry) || ((i || previous) && !checkpointKeyBefore(last, entry))) return false;
            last = entry;
        }
        if (!reader.finished()) return false;
        remaining_ = count_;
        valid_ = true;
        return true;
    }
    size_t count() const { return valid_ ? count_ : 0; }
    bool complete() const { return valid_ && !remaining_; }
    bool next(MutationView& entry)
    {
        entry = {};
        if (!valid_ || !remaining_) return false;
        if (!readCheckpointEntry(reader_, entry))
        {
            valid_ = false;
            return false;
        }
        --remaining_;
        return true;
    }

  private:
    protocol::CmpReader reader_{{}};
    size_t count_ = 0, remaining_ = 0;
    bool valid_ = false;
};

// Caller first validates the GCR1 page wrapper. Previous is the last entry of
// the preceding page, copied by the caller before reusing its input buffer.
inline bool decodeCheckpointPage(ByteView payload, uint64_t expected_index,
                                 MutationView* entries, size_t capacity, size_t& count,
                                 const MutationView* previous = nullptr)
{
    count = 0;
    CheckpointPageCursor cursor;
    if (!entries || !cursor.open(payload, expected_index, previous) || cursor.count() > capacity) return false;
    while (count < cursor.count())
        if (!cursor.next(entries[count++]))
        {
            count = 0;
            return false;
        }
    return cursor.complete();
}

struct CheckpointTailView
{
    uint64_t page_count = 0;
    uint64_t entry_count = 0;
    ByteView digest;
};

// Digest covers complete kind-3 GCR1 frames. Merely decoding this tail does not
// authenticate a checkpoint: the reader must compare counts and computed SHA256.
inline bool decodeCheckpointTail(ByteView payload, CheckpointTailView& out)
{
    out = {};
    if (!payload.data || payload.size > 65536) return false;
    protocol::CmpReader reader(payload);
    CheckpointTailView candidate;
    size_t fields = 0;
    if (!reader.array(fields, 3) || fields != 3 || !reader.unsignedInteger(candidate.page_count) ||
        !reader.unsignedInteger(candidate.entry_count) || !reader.binary(candidate.digest, 32) ||
        candidate.digest.size != 32 || !reader.finished()) return false;
    out = candidate;
    return true;
}
} // namespace geocaching::storage
