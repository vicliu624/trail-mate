#pragma once
#include "geocaching/storage/checkpoint_index_cursor.h"
#include "geocaching/storage/checkpoint_verifier.h"
#include "platform/esp/arduino_common/geocaching/sd_record_reader.h"

namespace platform::esp::arduino_common::geocaching
{
enum class CheckpointReadStep : uint8_t
{
    Reading,
    Verified,
    Invalid,
    IoError,
    WorkspaceTooSmall
};

// One-shot candidate reader. Keep tentative entries isolated from live state
// until Verified; restarting requires a fresh digest and reader instance.
template <class Digest>
class SdCheckpointReader
{
  public:
    explicit SdCheckpointReader(Digest& digest) : verifier_(digest) {}
    bool open(char slot)
    {
        if (started_ || (slot != 'a' && slot != 'b')) return false;
        started_ = true;
        slot_ = slot;
        if (!file_.open(slot == 'a' ? "/trailmate/geocaching/.state/checkpoint/a.gcs" : "/trailmate/geocaching/.state/checkpoint/b.gcs", "r")) return false;
        length_ = file_.size();
        return true;
    }
    CheckpointReadStep step(uint8_t* bytes, size_t capacity,
                            ::geocaching::storage::MutationView* entries, size_t entry_capacity, size_t& count)
    {
        count = 0;
        ::geocaching::storage::CheckpointPageCursor cursor;
        const auto result = stepCursor(bytes, capacity, cursor);
        if (result != CheckpointReadStep::Reading) return result;
        if ((cursor.complete() || cursor.count()) && (!entries || cursor.count() > entry_capacity))
            return state_ = CheckpointReadStep::WorkspaceTooSmall;
        while (count < cursor.count())
            if (!cursor.next(entries[count++]))
            {
                count = 0;
                return state_ = CheckpointReadStep::Invalid;
            }
        return state_;
    }
    // Cursor and derived index locations borrow bytes until the next step.
    // They remain tentative until the complete checkpoint reaches Verified.
    CheckpointReadStep stepCursor(uint8_t* bytes, size_t capacity, ::geocaching::storage::CheckpointPageCursor& cursor)
    {
        cursor = {};
        pending_frame_ = {};
        if (state_ != CheckpointReadStep::Reading) return state_;
        ::geocaching::storage::RecordFrameView frame;
        const auto read = readSdRecord(file_, length_, offset_, bytes, capacity, cursor_, frame);
        if (read == SegmentReadResult::InProgress) return state_;
        if (read == SegmentReadResult::End)
        {
            file_.close();
            return state_ = verifier_.finish() ? CheckpointReadStep::Verified : CheckpointReadStep::Invalid;
        }
        if (read == SegmentReadResult::IoError) return state_ = CheckpointReadStep::IoError;
        if (read == SegmentReadResult::WorkspaceTooSmall) return state_ = CheckpointReadStep::WorkspaceTooSmall;
        if (read != SegmentReadResult::Record ||
            !verifier_.accept({bytes, frame.payload.size + 24}, cursor))
            return state_ = CheckpointReadStep::Invalid;
        if (frame.kind == ::geocaching::storage::RecordKind::CheckpointPage)
        {
            pending_frame_ = {bytes, frame.payload.size + 24};
            pending_sequence_ = frame.sequence;
        }
        return state_;
    }
    bool pendingIndex(::geocaching::storage::CheckpointIndexCursor& cursor) const
    {
        cursor = {};
        if (state_ != CheckpointReadStep::Reading || !pending_frame_.data || offset_ < pending_frame_.size || offset_ > UINT32_MAX) return false;
        return cursor.open(pending_frame_, pending_sequence_, slot_, static_cast<uint32_t>(offset_ - pending_frame_.size));
    }
    uint64_t sequence() const { return verifier_.sequence(); }
    const std::array<uint8_t, 32>& digest() const { return verifier_.digest(); }

  private:
    storage::SdRuntimeFile file_;
    SdRecordReadCursor cursor_;
    ::geocaching::storage::CheckpointVerifier<Digest> verifier_;
    uint64_t length_ = 0, offset_ = 0;
    ::geocaching::ByteView pending_frame_;
    uint64_t pending_sequence_ = 0;
    char slot_ = 0;
    bool started_ = false;
    CheckpointReadStep state_ = CheckpointReadStep::Reading;
};
} // namespace platform::esp::arduino_common::geocaching
