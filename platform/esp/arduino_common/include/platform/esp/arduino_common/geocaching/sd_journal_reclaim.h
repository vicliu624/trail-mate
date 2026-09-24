#pragma once
#include "geocaching/storage/record_shape.h"
#include "geocaching/storage/transaction.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_inventory.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_segment.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
enum class JournalReclaimStep : uint8_t
{
    Idle,
    Working,
    Complete,
    Busy,
    Unavailable,
    Invalid,
    IoError,
    VolumeChanged,
    WorkspaceTooSmall
};

// Exclusive maintenance lease only. Both durable roots must already reference
// the new checkpoint exclusively, and the older retained checkpoint must have
// passed full verification. No live reader may reference any journal prefix.
// This verifies the retained suffix's framing, continuity and row shapes; it
// does not apply transactions or replace business validation during recovery.
// Only complete segments strictly before the retained suffix are removed.
class SdJournalReclaim
{
  public:
    explicit SdJournalReclaim(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(uint64_t retained_checkpoint, uint64_t current_sequence, uint8_t* frame, size_t capacity)
    {
        if (result_ != JournalReclaimStep::Idle || !retained_checkpoint || retained_checkpoint > current_sequence || !frame || capacity < 24) return false;
        const auto a = reinterpret_cast<uintptr_t>(this), b = reinterpret_cast<uintptr_t>(frame);
        if (a <= b ? b - a < sizeof(*this) : a - b < capacity) return false;
        retained_ = retained_checkpoint;
        current_ = current_sequence;
        frame_ = frame;
        capacity_ = capacity;
        result_ = JournalReclaimStep::Working;
        return true;
    }
    uint64_t removedSegments() const { return removed_; }
    JournalReclaimStep step()
    {
        using namespace ::geocaching::storage;
        using Result = JournalReclaimStep;
        if (result_ != Result::Working) return result_;
        if (storage::sd_external_block_owner_active())
        {
            interrupted_ = true;
            return Result::Busy;
        }
        if (!storage::sd_card_ready())
        {
            interrupted_ = true;
            return Result::Unavailable;
        }
        if (interrupted_)
        {
            // An external owner may have modified the card. Discard every
            // cursor and re-prove the suffix before any subsequent deletion.
            segment_.reset();
            inventory_.reset();
            directory_.close();
            interrupted_ = false;
            check_volume_ = true;
            phase_ = Phase::Inventory;
            return result_;
        }
        if (check_volume_)
        {
            VolumeInstance current;
            const auto checked = inspectSdVolume(current);
            if (checked == SdVolumeResult::Unavailable)
            {
                interrupted_ = true;
                return Result::Unavailable;
            }
            if (checked != SdVolumeResult::Ready) return fail(Result::IoError);
            if (current != volume_) return fail(Result::VolumeChanged);
            check_volume_ = false;
            return result_;
        }
        check_volume_ = true;
        switch (phase_)
        {
        case Phase::Inventory:
            if (!inventory_)
            {
                inventory_.emplace(volume_, retained_);
                return result_;
            }
            {
                const auto status = inventory_->step();
                if (status == InventoryStep::Scanning) return result_;
                if (status != InventoryStep::Complete) return fail(status == InventoryStep::VolumeChanged ? Result::VolumeChanged : status == InventoryStep::RetryLater ? Result::IoError
                                                                                                                                                                        : Result::Invalid);
                range_ = inventory_->range();
                inventory_.reset();
                confirmed_ = retained_;
                if (!range_.last_start) return fail(current_ == retained_ ? Result::Complete : Result::Invalid);
                start_ = range_.first_start;
                phase_ = Phase::OpenSuffix;
                return result_;
            }
        case Phase::OpenSuffix:
        case Phase::OpenPrefix:
            segment_.emplace();
            if (!segment_->open(start_)) return fail(Result::IoError);
            last_ = 0;
            prefix_ = phase_ == Phase::OpenPrefix;
            phase_ = prefix_ ? Phase::ReadPrefix : Phase::ReadSuffix;
            return result_;
        case Phase::ReadSuffix:
        case Phase::ReadPrefix:
        {
            RecordFrameView frame;
            const auto status = segment_->next(frame_, capacity_, frame);
            if (status == SegmentReadResult::InProgress) return result_;
            if (status == SegmentReadResult::End)
            {
                segment_.reset();
                if (!last_) return fail(Result::Invalid);
                if (prefix_)
                {
                    if (last_ > retained_ || last_ >= range_.first_start) return fail(Result::Invalid);
                    phase_ = Phase::Remove;
                }
                else if (start_ == range_.last_start)
                {
                    if (confirmed_ != current_) return fail(Result::Invalid);
                    phase_ = Phase::OpenDirectory;
                }
                else
                {
                    if (last_ == UINT64_MAX || last_ + 1 > range_.last_start) return fail(Result::Invalid);
                    start_ = last_ + 1;
                    phase_ = Phase::OpenSuffix;
                }
                return result_;
            }
            if (status != SegmentReadResult::Record) return fail(status == SegmentReadResult::IoError ? Result::IoError : status == SegmentReadResult::WorkspaceTooSmall ? Result::WorkspaceTooSmall
                                                                                                                                                                         : Result::Invalid);
            if (!frame.sequence || frame.sequence > current_ ||
                (!prefix_ && frame.sequence > retained_ && (confirmed_ == UINT64_MAX || frame.sequence != confirmed_ + 1)) ||
                (prefix_ && (frame.sequence > retained_ || frame.sequence >= range_.first_start)) ||
                !validateTransaction(frame.payload, frame.sequence - 1)) return fail(Result::Invalid);
            last_ = frame.sequence;
            row_reader_ = ::geocaching::protocol::CmpReader(frame.payload);
            if (!readTransactionHeader(row_reader_, frame.sequence - 1, remaining_)) return fail(Result::Invalid);
            phase_ = Phase::Rows;
            return result_;
        }
        case Phase::Rows:
            if (remaining_)
            {
                MutationView row;
                if (!readTransactionMutation(row_reader_, row) || !validStoredRowShape(row)) return fail(Result::Invalid);
                --remaining_;
                return result_;
            }
            if (!row_reader_.finished()) return fail(Result::Invalid);
            if (!prefix_ && last_ > confirmed_) confirmed_ = last_;
            phase_ = prefix_ ? Phase::ReadPrefix : Phase::ReadSuffix;
            return result_;
        case Phase::OpenDirectory:
            if (!directory_.open("/trailmate/geocaching/.state/journal")) return fail(Result::IoError);
            phase_ = Phase::ReadDirectory;
            return result_;
        case Phase::ReadDirectory:
        {
            char name[32]{};
            bool is_directory = false;
            const auto status = directory_.read_next_status(name, sizeof(name), &is_directory);
            if (status == storage::SdDirReadStatus::End) return fail(Result::Complete);
            if (status == storage::SdDirReadStatus::Busy)
            {
                interrupted_ = true;
                return Result::Busy;
            }
            if (status == storage::SdDirReadStatus::Unavailable)
            {
                interrupted_ = true;
                return Result::Unavailable;
            }
            if (status != storage::SdDirReadStatus::Entry) return fail(status == storage::SdDirReadStatus::Invalid ? Result::Invalid : Result::IoError);
            if (is_directory || !decodeName(name, start_)) return fail(Result::Invalid);
            if (start_ >= range_.first_start) return result_;
            directory_.close();
            phase_ = Phase::OpenPrefix;
            return result_;
        }
        case Phase::Remove:
        {
            char path[80];
            std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/journal/%016llx.gcj", static_cast<unsigned long long>(start_));
            if (!storage::sd_remove(path)) return fail(Result::IoError);
            ++removed_;
            phase_ = Phase::OpenDirectory;
            return result_;
        }
        }
        return fail(Result::Invalid);
    }

  private:
    static bool decodeName(const char* name, uint64_t& sequence)
    {
        sequence = 0;
        if (std::strlen(name) != 20 || std::strcmp(name + 16, ".gcj")) return false;
        for (unsigned i = 0; i < 16; ++i)
        {
            const auto c = name[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            sequence = (sequence << 4) | static_cast<uint64_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        return sequence != 0;
    }
    JournalReclaimStep fail(JournalReclaimStep status)
    {
        segment_.reset();
        inventory_.reset();
        directory_.close();
        return result_ = status;
    }
    enum class Phase : uint8_t
    {
        Inventory,
        OpenSuffix,
        ReadSuffix,
        Rows,
        OpenDirectory,
        ReadDirectory,
        OpenPrefix,
        ReadPrefix,
        Remove
    };
    ::geocaching::storage::VolumeInstance volume_;
    JournalSegmentRange range_;
    std::optional<SdJournalInventory> inventory_;
    std::optional<SdJournalSegment> segment_;
    storage::SdRuntimeDir directory_;
    ::geocaching::protocol::CmpReader row_reader_{{}};
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, remaining_ = 0;
    uint64_t retained_ = 0, current_ = 0, confirmed_ = 0, start_ = 0, last_ = 0, removed_ = 0;
    bool check_volume_ = true, interrupted_ = false, prefix_ = false;
    Phase phase_ = Phase::Inventory;
    JournalReclaimStep result_ = JournalReclaimStep::Idle;
};
static_assert(sizeof(SdJournalReclaim) <= 640, "Journal reclamation retains cursors, never a file list or ledger");
} // namespace platform::esp::arduino_common::geocaching
