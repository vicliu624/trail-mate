#pragma once
#include "geocaching/storage/record_shape.h"
#include "platform/esp/arduino_common/geocaching/sd_index_transaction.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_replay.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexReplayStep : uint8_t
{
    Working,
    NeedsValidation,
    Complete,
    RetryLater,
    IoError,
    OutOfMemory,
    Invalid,
    VolumeChanged,
    RecoveryRequired
};

// The root pair has been selected/validated and range comes from a completed
// journal inventory. All buffers are caller leases; no logical table is loaded.
// Business validation must inspect the resulting state before accept(true).
class SdIndexReplay
{
  public:
    SdIndexReplay(const ::geocaching::storage::VolumeInstance& volume,
                  const ::geocaching::storage::IndexRootView& root, unsigned copy,
                  ::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second,
                  JournalSegmentRange range, uint8_t* frame, size_t capacity,
                  ::geocaching::storage::MutationView* mutations, size_t mutation_capacity)
        : volume_(volume), root_(root), copy_(copy), roots_{&first, &second},
          replay_(volume, root.sequence, range, frame, capacity, mutations, mutation_capacity)
    {
        if (copy > 1 || &first == &second || !::geocaching::storage::validIndexRoot(root) ||
            root.shards.data != roots_[copy]->data() + 48) result_ = IndexReplayStep::Invalid;
        const auto overlaps = [](const void* a, size_t as, const void* b, size_t bs)
        {
            const auto x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
            return as && bs && (x <= y ? y - x < as : x - y < bs);
        };
        if (!frame || !mutations || mutation_capacity > 64 ||
            overlaps(frame, capacity, first.data(), first.size()) || overlaps(frame, capacity, second.data(), second.size()) ||
            overlaps(mutations, mutation_capacity * sizeof(*mutations), frame, capacity) ||
            overlaps(mutations, mutation_capacity * sizeof(*mutations), first.data(), first.size()) ||
            overlaps(mutations, mutation_capacity * sizeof(*mutations), second.data(), second.size())) result_ = IndexReplayStep::Invalid;
    }
    bool pending(::geocaching::storage::TransactionView& transaction) const
    {
        transaction = {};
        if (result_ != IndexReplayStep::NeedsValidation) return false;
        transaction = pending_;
        return true;
    }
    ::geocaching::storage::IndexRootView base() const { return root_; }
    bool selected(::geocaching::storage::IndexRootView& root, unsigned& copy) const
    {
        root = {};
        if (result_ != IndexReplayStep::Complete) return false;
        root = root_;
        copy = copy_;
        return true;
    }
    // The caller supplies a separate temporary read lease because replay's
    // transaction bytes remain pinned while references are checked on disk.
    bool accept(bool valid, uint8_t* validation_frame = nullptr, size_t validation_capacity = 0)
    {
        if (result_ != IndexReplayStep::NeedsValidation) return false;
        if (!valid)
        {
            result_ = IndexReplayStep::Invalid;
            return true;
        }
        const auto pending_frame = replay_.pendingFrame();
        const auto a = reinterpret_cast<uintptr_t>(pending_frame.data), b = reinterpret_cast<uintptr_t>(validation_frame);
        if (!validation_frame || !validation_capacity ||
            (a <= b ? b - a < pending_frame.size : a - b < validation_capacity)) return false;
        validation_.reset(new (std::nothrow) SdIndexedCommit(volume_));
        if (!validation_)
        {
            result_ = IndexReplayStep::OutOfMemory;
            return true;
        }
        if (!validation_->begin(root_, copy_, pending_.mutations, pending_.count,
                                validation_frame, validation_capacity, *roots_[1 - copy_], true))
        {
            // A failed begin leaves a terminal result. Read it before releasing
            // the validator so allocator pressure cannot masquerade as damage.
            result_ = validation_->step() == IndexedCommitStep::OutOfMemory ? IndexReplayStep::OutOfMemory : IndexReplayStep::Invalid;
            validation_.reset();
        }
        else result_ = IndexReplayStep::Working;
        return true;
    }
    IndexReplayStep step()
    {
        if (result_ != IndexReplayStep::Working && result_ != IndexReplayStep::RetryLater) return result_;
        result_ = IndexReplayStep::Working;
        if (validation_)
        {
            const auto status = validation_->step();
            if (status == IndexedCommitStep::Working) return result_;
            validation_.reset();
            if (status == IndexedCommitStep::IoError) return result_ = IndexReplayStep::IoError;
            if (status != IndexedCommitStep::Validated)
                return result_ = status == IndexedCommitStep::VolumeChanged ? IndexReplayStep::VolumeChanged : IndexReplayStep::Invalid;
            const auto frame = replay_.pendingFrame();
            ::geocaching::storage::TransactionIndexCursor cursor;
            ::geocaching::storage::IndexedMutation first;
            if (frame.size < 24 || std::memcmp(frame.data + 20, pending_crc_.data(), pending_crc_.size()) ||
                !replay_.pendingIndex(cursor) || !cursor.next(first) ||
                !index_.emplace(volume_).begin(root_, copy_, frame, first.location.segment_first_sequence,
                                               first.location.frame_offset, *roots_[1 - copy_])) return result_ = IndexReplayStep::Invalid;
            return result_;
        }
        if (index_)
        {
            const auto status = index_->step();
            if (status == IndexTransactionStep::Working) return result_;
            if (status == IndexTransactionStep::IoError) return result_ = IndexReplayStep::IoError;
            if (status == IndexTransactionStep::VolumeChanged) return result_ = IndexReplayStep::VolumeChanged;
            ::geocaching::storage::IndexRootView committed;
            if (status != IndexTransactionStep::Verified || !index_->committed(committed) ||
                !replay_.acknowledgeApplied(committed.sequence)) return result_ = IndexReplayStep::RecoveryRequired;
            root_ = committed;
            copy_ = 1 - copy_;
            index_.reset();
            pending_ = {};
            return result_;
        }
        const auto status = replay_.next(pending_);
        if (status == ReplayStep::Advancing) return result_;
        if (status == ReplayStep::RetryLater) return result_ = IndexReplayStep::RetryLater;
        if (status == ReplayStep::VolumeChanged) return result_ = IndexReplayStep::VolumeChanged;
        if (status == ReplayStep::JournalComplete) return result_ = IndexReplayStep::Complete;
        if (status != ReplayStep::Transaction) return result_ = IndexReplayStep::RecoveryRequired;
        for (size_t i = 0; i < pending_.count; ++i)
            if (!::geocaching::storage::validStoredRowShape(pending_.mutations[i])) return result_ = IndexReplayStep::Invalid;
        const auto frame = replay_.pendingFrame();
        if (frame.size < 24) return result_ = IndexReplayStep::Invalid;
        std::memcpy(pending_crc_.data(), frame.data + 20, pending_crc_.size());
        return result_ = IndexReplayStep::NeedsValidation;
    }

  private:
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    unsigned copy_;
    ::geocaching::storage::IndexRootBytes* roots_[2];
    SdJournalReplay replay_;
    std::optional<SdIndexTransaction> index_;
    std::unique_ptr<SdIndexedCommit> validation_;
    std::array<uint8_t, 4> pending_crc_{};
    ::geocaching::storage::TransactionView pending_;
    IndexReplayStep result_ = IndexReplayStep::Working;
};
static_assert(sizeof(SdIndexReplay) <= 1280, "Indexed replay must not retain a logical ledger");
} // namespace platform::esp::arduino_common::geocaching
