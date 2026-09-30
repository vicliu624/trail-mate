#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
// Both root metadata leases, their shard heads and the caller's frame remain
// pinned while this runs. Caller must exclude writers and drain older readers.
// An unreferenced slot still needs checkpoint-fallback/publication validation;
// this class proves only the absence of live references in these two roots.
class SdCheckpointReferences
{
  public:
    explicit SdCheckpointReferences(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& first,
               const ::geocaching::storage::IndexRootView& second, uint8_t* frame, size_t capacity)
    {
        if (result_ != IndexScanStep::Idle) return false;
        ::geocaching::storage::IndexRootView selected;
        if (!::geocaching::storage::selectIndexRoot(first, second, selected) || !frame || capacity < 24)
        {
            result_ = IndexScanStep::Invalid;
            return false;
        }
        // Check BOTH leases before the first scan writes anything. Deferring
        // this check until scan_->begin(second) can destroy the second bitmap.
        const auto overlaps = [&](const void* data, size_t size)
        {
            const auto a = reinterpret_cast<uintptr_t>(data), b = reinterpret_cast<uintptr_t>(frame);
            return a <= b ? b - a < size : a - b < capacity;
        };
        if (overlaps(first.shards.data, first.shards.size) || overlaps(second.shards.data, second.shards.size) ||
            overlaps(this, sizeof(*this)) || overlaps(&first, sizeof(first)) || overlaps(&second, sizeof(second)))
        {
            result_ = IndexScanStep::Invalid;
            return false;
        }
        roots_[0] = first;
        roots_[1] = second;
        frame_ = frame;
        capacity_ = capacity;
        result_ = IndexScanStep::Working;
        return true;
    }
    IndexScanStep step()
    {
        if (result_ != IndexScanStep::Working) return result_;
        if (!scan_)
        {
            if (root_ == 2) return result_ = IndexScanStep::End;
            scan_.emplace(volume_);
            if (!scan_->begin(roots_[root_], table_, frame_, capacity_)) return fail(IndexScanStep::Invalid);
            return result_;
        }
        const auto status = scan_->step();
        if (status == IndexScanStep::Working) return result_;
        if (status == IndexScanStep::Item)
        {
            ::geocaching::storage::IndexedMutation entry;
            if (!scan_->indexedItem(entry)) return fail(IndexScanStep::Invalid);
            using Source = ::geocaching::storage::IndexedValueSource;
            if (entry.location.source == Source::CheckpointA) slots_ |= 1;
            if (entry.location.source == Source::CheckpointB) slots_ |= 2;
            if (!scan_->advance()) return fail(IndexScanStep::Invalid);
            return result_;
        }
        if (status != IndexScanStep::End) return fail(status);
        scan_.reset();
        if (++table_ == 14)
        {
            table_ = 1;
            ++root_;
        }
        return result_;
    }
    // Fail closed while incomplete or failed, including invalid slot names.
    bool referenced(char slot) const
    {
        if (result_ != IndexScanStep::End || (slot != 'a' && slot != 'b')) return true;
        return (slots_ & (slot == 'a' ? 1 : 2)) != 0;
    }

  private:
    IndexScanStep fail(IndexScanStep status)
    {
        scan_.reset();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView roots_[2];
    std::optional<SdIndexScan> scan_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    uint8_t root_ = 0, table_ = 1, slots_ = 0;
    IndexScanStep result_ = IndexScanStep::Idle;
};
static_assert(sizeof(SdCheckpointReferences) <= 1024, "Checkpoint reference audit retains one scan only");
} // namespace platform::esp::arduino_common::geocaching
