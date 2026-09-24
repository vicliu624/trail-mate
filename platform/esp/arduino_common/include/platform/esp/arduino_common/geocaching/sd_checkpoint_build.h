#pragma once
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_writer.h"
#include "platform/esp/arduino_common/geocaching/sd_sorted_index.h"

namespace platform::esp::arduino_common::geocaching
{
enum class CheckpointBuildStep : uint8_t
{
    Idle,
    Working,
    Complete,
    Busy,
    Unavailable,
    OutOfMemory,
    Invalid,
    IoError,
    VolumeChanged
};

// Produces a complete, read-back-verified staging checkpoint from a pinned live
// index. Publication, root replacement and journal reclamation remain the
// coordinator's responsibility. Neither authoritative checkpoint slot is edited.
template <class Digest>
class SdCheckpointBuild
{
  public:
    explicit SdCheckpointBuild(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, uint8_t* frame, size_t capacity,
               void* sort_buffer, size_t sort_capacity)
    {
        if (result_ != CheckpointBuildStep::Idle || !root.sequence) return false;
        sort_.reset(new (std::nothrow) SdSortedIndex(volume_));
        if (!sort_)
        {
            fail(CheckpointBuildStep::OutOfMemory);
            return false;
        }
        if (!sort_->begin(root, frame, capacity, sort_buffer, sort_capacity))
        {
            fail(CheckpointBuildStep::Invalid);
            return false;
        }
        sequence_ = root.sequence;
        frame_ = frame;
        capacity_ = capacity;
        result_ = CheckpointBuildStep::Working;
        return true;
    }
    CheckpointBuildStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != CheckpointBuildStep::Working) return result_;
        if (storage::sd_external_block_owner_active()) return CheckpointBuildStep::Busy;
        if (!storage::sd_card_ready()) return CheckpointBuildStep::Unavailable;
        if (phase_ == Phase::Sort)
        {
            const auto status = sort_->step();
            if (status == SortedIndexStep::Busy) return CheckpointBuildStep::Busy;
            if (status == SortedIndexStep::Unavailable) return CheckpointBuildStep::Unavailable;
            if (status == SortedIndexStep::OutOfMemory) return fail(CheckpointBuildStep::OutOfMemory);
            if (status == SortedIndexStep::Working) return result_;
            if (status != SortedIndexStep::Complete) return fail(status == SortedIndexStep::VolumeChanged ? CheckpointBuildStep::VolumeChanged
                                                                 : status == SortedIndexStep::IoError     ? CheckpointBuildStep::IoError
                                                                                                          : CheckpointBuildStep::Invalid);
            sorted_path_ = sort_->sortedPath();
            total_ = sort_->count();
            sort_.reset();
            phase_ = Phase::Volume;
            return result_;
        }
        if (phase_ == Phase::Volume)
        {
            VolumeInstance current;
            const auto checked = inspectSdVolume(current);
            if (checked == SdVolumeResult::Unavailable) return CheckpointBuildStep::Unavailable;
            if (checked != SdVolumeResult::Ready) return fail(CheckpointBuildStep::IoError);
            if (current != volume_) return fail(CheckpointBuildStep::VolumeChanged);
            phase_ = Phase::DiscardStaging;
            return result_;
        }
        if (phase_ == Phase::DiscardStaging)
        {
            // An index cannot reference this pathname. A previous interrupted
            // build is disposable while the exclusive maintenance lease is held.
            if (storage::sd_exists(SdCheckpointWriter<Digest>::path) && !storage::sd_remove(SdCheckpointWriter<Digest>::path)) return fail(CheckpointBuildStep::IoError);
            phase_ = Phase::Open;
            return result_;
        }
        if (phase_ == Phase::Open)
        {
            if (!file_.open(sorted_path_, "r") || file_.size() != total_ * kIndexEntrySize) return fail(CheckpointBuildStep::IoError);
            writer_.reset(new (std::nothrow) SdCheckpointWriter<Digest>(volume_, digest_));
            if (!writer_) return fail(CheckpointBuildStep::OutOfMemory);
            if (!writer_->begin(sequence_)) return fail(CheckpointBuildStep::Invalid);
            phase_ = Phase::Write;
            return result_;
        }
        if (phase_ == Phase::Write)
        {
            const auto status = writer_->step();
            if (status == CheckpointWriteStep::Busy) return CheckpointBuildStep::Busy;
            if (status == CheckpointWriteStep::Unavailable) return CheckpointBuildStep::Unavailable;
            if (status == CheckpointWriteStep::Working) return result_;
            if (status == CheckpointWriteStep::Verified) return fail(CheckpointBuildStep::Complete);
            if (status != CheckpointWriteStep::Ready) return fail(status == CheckpointWriteStep::VolumeChanged ? CheckpointBuildStep::VolumeChanged
                                                                  : status == CheckpointWriteStep::IoError     ? CheckpointBuildStep::IoError
                                                                                                               : CheckpointBuildStep::Invalid);
            value_.reset();
            reference_read_ = 0;
            if (consumed_ == total_)
            {
                if (file_.size() != total_ * kIndexEntrySize || !writer_->finish()) return fail(CheckpointBuildStep::Invalid);
                file_.close();
            }
            else phase_ = Phase::ReadReference;
            return result_;
        }
        if (phase_ == Phase::ReadReference)
        {
            const int count = file_.read(reference_.data() + reference_read_, reference_.size() - reference_read_);
            if (count <= 0 || static_cast<size_t>(count) > reference_.size() - reference_read_) return fail(CheckpointBuildStep::IoError);
            reference_read_ += static_cast<size_t>(count);
            if (reference_read_ != reference_.size()) return result_;
            if (!decodeIndexEntry({reference_.data(), reference_.size()}, volume_, entry_) || entry_.erase) return fail(CheckpointBuildStep::Invalid);
            value_.reset(new (std::nothrow) SdIndexedValueReader(volume_));
            if (!value_) return fail(CheckpointBuildStep::OutOfMemory);
            if (!value_->begin(entry_, frame_, capacity_)) return fail(CheckpointBuildStep::Invalid);
            phase_ = Phase::ReadValue;
            return result_;
        }
        const auto status = value_->step();
        if (status == IndexedReadStep::Working) return result_;
        if (status != IndexedReadStep::Ready) return fail(status == IndexedReadStep::VolumeChanged ? CheckpointBuildStep::VolumeChanged
                                                          : status == IndexedReadStep::IoError     ? CheckpointBuildStep::IoError
                                                                                                   : CheckpointBuildStep::Invalid);
        row_ = {entry_.table, entry_.key, value_->value(), false};
        if (!validStoredRowShape(row_) || !writer_->page(&row_, 1)) return fail(CheckpointBuildStep::Invalid);
        ++consumed_;
        phase_ = Phase::Write;
        return result_;
    }

  private:
    enum class Phase : uint8_t
    {
        Sort,
        Volume,
        DiscardStaging,
        Open,
        Write,
        ReadReference,
        ReadValue
    };
    CheckpointBuildStep fail(CheckpointBuildStep result)
    {
        writer_.reset();
        value_.reset();
        sort_.reset();
        file_.close();
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    Digest digest_;
    std::unique_ptr<SdSortedIndex> sort_;
    std::unique_ptr<SdCheckpointWriter<Digest>> writer_;
    std::unique_ptr<SdIndexedValueReader> value_;
    storage::SdRuntimeFile file_;
    ::geocaching::storage::IndexEntryBytes reference_{};
    ::geocaching::storage::IndexedMutation entry_;
    ::geocaching::storage::MutationView row_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, reference_read_ = 0;
    uint64_t sequence_ = 0, total_ = 0, consumed_ = 0;
    const char* sorted_path_ = nullptr;
    Phase phase_ = Phase::Sort;
    CheckpointBuildStep result_ = CheckpointBuildStep::Idle;
};
} // namespace platform::esp::arduino_common::geocaching
