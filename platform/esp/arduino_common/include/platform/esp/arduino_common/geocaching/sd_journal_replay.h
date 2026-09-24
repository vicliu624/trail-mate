#pragma once
#include "geocaching/storage/logical_state.h"
#include "geocaching/storage/transaction_index_cursor.h"
#include "platform/esp/arduino_common/geocaching/sd_journal.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_inventory.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_segment.h"

namespace platform::esp::arduino_common::geocaching
{
enum class ReplayStep : uint8_t
{
    Transaction,
    Advancing,
    JournalComplete,
    RetryLater,
    Corrupt,
    VolumeChanged,
    TailTruncated,
    Applied,
    ApplicationRejected
};

// Range comes from a completed segment inventory. Caller supplies worker buffers.
// JournalComplete does not imply GPX-install recovery or business hydration.
class SdJournalReplay
{
  public:
    SdJournalReplay(const ::geocaching::storage::VolumeInstance& volume,
                    uint64_t checkpoint_sequence, JournalSegmentRange range,
                    uint8_t* bytes, size_t capacity,
                    ::geocaching::storage::MutationView* mutations, size_t mutation_capacity)
        : volume_(volume), applied_(checkpoint_sequence), range_(range), segment_start_(range.first_start), bytes_(bytes),
          capacity_(capacity), mutations_(mutations), mutation_capacity_(mutation_capacity),
          corrupt_(!range.complete || range.first_start > range.last_start ||
                   (!range.first_start && range.last_start) || !bytes || !mutations || capacity < 24) {}

    uint64_t appliedSequence() const { return applied_; }
    ::geocaching::ByteView pendingFrame() const
    {
        return !corrupt_ && pending_.count ? ::geocaching::ByteView{bytes_, pending_frame_size_} : ::geocaching::ByteView{};
    }
    // Owner may stage derived index entries while this transaction is pending.
    // They must not become visible until its value/reference validation passes.
    // The frame lease ends at acknowledgeApplied()/the next record read.
    bool pendingIndex(::geocaching::storage::TransactionIndexCursor& cursor) const
    {
        cursor = {};
        return !corrupt_ && pending_.count && pending_frame_size_ &&
               cursor.open({bytes_, pending_frame_size_}, applied_, segment_start_, pending_frame_offset_);
    }

    // Serialized owner convenience: validate/apply the complete candidate
    // before acknowledging its sequence. Rejected transactions remain pending.
    template <class Validate>
    ReplayStep applyNext(::geocaching::storage::LogicalState& state, Validate validate)
    {
        ::geocaching::storage::TransactionView transaction;
        const auto result = next(transaction);
        if (result != ReplayStep::Transaction) return result;
        if (!state.apply(transaction.mutations, transaction.count, validate)) return ReplayStep::ApplicationRejected;
        // next() proved exactly one sequential transaction, so acknowledgment
        // cannot fail under this object's single-owner contract.
        if (!acknowledgeApplied(applied_ + 1))
        {
            corrupt_ = true;
            return ReplayStep::Corrupt;
        }
        return ReplayStep::Applied;
    }

    ReplayStep next(::geocaching::storage::TransactionView& out)
    {
        out = {};
        if (corrupt_) return ReplayStep::Corrupt;
        if (!volume_checked_)
        {
            ::geocaching::storage::VolumeInstance current;
            const auto volume = inspectSdVolume(current);
            if (volume == SdVolumeResult::Missing || volume == SdVolumeResult::Unavailable || volume == SdVolumeResult::IoError)
                return ReplayStep::RetryLater;
            if (volume != SdVolumeResult::Ready)
            {
                corrupt_ = true;
                return ReplayStep::Corrupt;
            }
            if (current != volume_) return ReplayStep::VolumeChanged;
            volume_checked_ = true;
            return ReplayStep::Advancing;
        }
        volume_checked_ = false;
        if (pending_.count)
        {
            out = pending_;
            return ReplayStep::Transaction;
        }
        if (!range_.last_start) return ReplayStep::JournalComplete;
        if (!opened_)
        {
            if (probed_)
            {
                probed_ = false;
                if (!segment_.open(segment_start_)) return ReplayStep::RetryLater;
                opened_ = true;
                return ReplayStep::Advancing;
            }
            char path[80]{};
            std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/journal/%016llx.gcj",
                          static_cast<unsigned long long>(segment_start_));
            uint8_t probe[24];
            const auto status = storage::sd_read_file(path, probe, sizeof(probe));
            if (status.status == storage::SdFileReadStatus::Missing)
            {
                corrupt_ = true;
                return ReplayStep::Corrupt;
            }
            if (status.status != storage::SdFileReadStatus::Ready && status.status != storage::SdFileReadStatus::Invalid)
                return ReplayStep::RetryLater;
            if (!status.file_size || status.file_size > 1024U * 1024U)
            {
                corrupt_ = true;
                return ReplayStep::Corrupt;
            }
            probed_ = true;
            return ReplayStep::Advancing;
        }
        ::geocaching::storage::RecordFrameView frame;
        const auto result = segment_.next(bytes_, capacity_, frame);
        if (result == SegmentReadResult::InProgress) return ReplayStep::Advancing;
        if (result == SegmentReadResult::WorkspaceTooSmall) return ReplayStep::ApplicationRejected;
        if (result == SegmentReadResult::IoError)
        {
            opened_ = false;
            return ReplayStep::RetryLater;
        }
        if (result == SegmentReadResult::Truncated && segment_start_ == range_.last_start) return ReplayStep::TailTruncated;
        if (result == SegmentReadResult::End)
        {
            if (segment_start_ == range_.last_start) return ReplayStep::JournalComplete;
            if (applied_ == UINT64_MAX || applied_ + 1 > range_.last_start)
            {
                corrupt_ = true;
                return ReplayStep::Corrupt;
            }
            segment_start_ = applied_ + 1;
            opened_ = false;
            return ReplayStep::Advancing;
        }
        if (result != SegmentReadResult::Record || frame.sequence == 0 ||
            !::geocaching::storage::decodeTransaction(frame.payload, frame.sequence - 1, mutations_, mutation_capacity_, pending_))
        {
            corrupt_ = true;
            return ReplayStep::Corrupt;
        }
        if (frame.sequence <= applied_)
        {
            pending_ = {};
            return ReplayStep::Advancing;
        }
        if (applied_ == UINT64_MAX || frame.sequence != applied_ + 1)
        {
            pending_ = {};
            corrupt_ = true;
            return ReplayStep::Corrupt;
        }
        pending_frame_size_ = static_cast<uint32_t>(24 + frame.payload.size);
        if (segment_.position() < pending_frame_size_ || segment_.position() > UINT32_MAX)
        {
            pending_ = {};
            corrupt_ = true;
            return ReplayStep::Corrupt;
        }
        pending_frame_offset_ = static_cast<uint32_t>(segment_.position() - pending_frame_size_);
        out = pending_;
        return ReplayStep::Transaction;
    }

    // Call only after validating values/references and atomically applying the
    // entire pending transaction. A failed application leaves the cursor put.
    bool acknowledgeApplied(uint64_t sequence)
    {
        if (corrupt_ || !pending_.count || applied_ == UINT64_MAX || sequence != applied_ + 1) return false;
        applied_ = sequence;
        pending_ = {};
        pending_frame_size_ = 0;
        return true;
    }

  private:
    const ::geocaching::storage::VolumeInstance volume_;
    uint64_t applied_;
    JournalSegmentRange range_;
    uint64_t segment_start_;
    SdJournalSegment segment_;
    bool opened_ = false;
    bool volume_checked_ = false, probed_ = false;
    uint32_t pending_frame_size_ = 0, pending_frame_offset_ = 0;
    uint8_t* bytes_;
    size_t capacity_;
    ::geocaching::storage::MutationView* mutations_;
    size_t mutation_capacity_;
    bool corrupt_;
    ::geocaching::storage::TransactionView pending_;
};
} // namespace platform::esp::arduino_common::geocaching
