#pragma once
#include "geocaching/storage/checkpoint.h"
#include "geocaching/storage/record_frame.h"

namespace geocaching::storage
{
// Digest implements update(data,size) and finalize(output,size), matching the
// existing platform Sha256Digest. Use a fresh digest for each candidate file.
// Page entries are tentative until finish() succeeds at actual file EOF.
template <class Digest>
class CheckpointVerifier
{
  public:
    explicit CheckpointVerifier(Digest& digest) : digest_(digest) {}
    bool accept(ByteView encoded_frame, MutationView* entries, size_t capacity, size_t& count)
    {
        count = 0;
        CheckpointPageCursor cursor;
        if (!accept(encoded_frame, cursor)) return false;
        if ((cursor.complete() || cursor.count()) && (!entries || cursor.count() > capacity)) return fail();
        while (count < cursor.count())
            if (!cursor.next(entries[count++]))
            {
                count = 0;
                return fail();
            }
        return true;
    }
    bool accept(ByteView encoded_frame, CheckpointPageCursor& cursor)
    {
        cursor = {};
        if (failed_ || tail_seen_ || complete_) return fail();
        RecordFrameView frame;
        if (!decodeRecordFrame(encoded_frame, frame)) return fail();
        if (!has_sequence_)
        {
            sequence_ = frame.sequence;
            has_sequence_ = true;
        }
        if (frame.sequence != sequence_) return fail();
        if (frame.kind == RecordKind::CheckpointPage)
        {
            MutationView prior{last_table_, {last_key_.data(), last_key_size_}, {}, false};
            if (pages_ == UINT64_MAX || !cursor.open(frame.payload, pages_, last_key_size_ ? &prior : nullptr) ||
                cursor.count() > UINT64_MAX - entry_count_) return fail();
            auto scan = cursor;
            MutationView last;
            while (scan.next(last))
            {
                last_table_ = last.table;
                last_key_size_ = last.key.size;
                std::memcpy(last_key_.data(), last.key.data, last.key.size);
            }
            if (!scan.complete()) return fail();
            digest_.update(encoded_frame.data, encoded_frame.size);
            ++pages_;
            entry_count_ += cursor.count();
            return true;
        }
        if (frame.kind != RecordKind::CheckpointTail) return fail();
        CheckpointTailView tail;
        if (!decodeCheckpointTail(frame.payload, tail) || tail.page_count != pages_ || tail.entry_count != entry_count_ ||
            !digest_.finalize(digest_bytes_.data(), digest_bytes_.size()) ||
            std::memcmp(tail.digest.data, digest_bytes_.data(), digest_bytes_.size())) return fail();
        tail_seen_ = true;
        return true;
    }
    bool finish()
    {
        if (failed_ || !tail_seen_) return fail();
        complete_ = true;
        return true;
    }
    bool verified() const { return complete_ && !failed_; }
    uint64_t sequence() const { return verified() ? sequence_ : 0; }
    const std::array<uint8_t, 32>& digest() const { return digest_bytes_; }

  private:
    bool fail()
    {
        failed_ = true;
        return false;
    }
    Digest& digest_;
    uint64_t sequence_ = 0, pages_ = 0, entry_count_ = 0;
    std::array<uint8_t, 96> last_key_{};
    std::array<uint8_t, 32> digest_bytes_{};
    size_t last_key_size_ = 0;
    uint8_t last_table_ = 0;
    bool has_sequence_ = false, tail_seen_ = false, complete_ = false, failed_ = false;
};
} // namespace geocaching::storage
