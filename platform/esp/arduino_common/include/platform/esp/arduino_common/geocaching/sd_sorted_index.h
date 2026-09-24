#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include <algorithm>
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching
{
enum class SortedIndexStep : uint8_t
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

// Exclusive maintenance lease: the root and all backing values stay pinned.
// Sort only index references, not ledger values. Fixed-size sorted runs are
// merged on disk; memory use does not depend on the number of live records.
class SdSortedIndex
{
  public:
    using Entry = ::geocaching::storage::IndexEntryBytes;
    using Run = std::array<Entry, 32>;
    explicit SdSortedIndex(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, uint8_t* frame, size_t capacity,
               void* run_buffer, size_t run_capacity)
    {
        if (result_ != SortedIndexStep::Idle || !::geocaching::storage::validIndexRoot(root) ||
            !frame || capacity < 24 || !run_buffer || run_capacity < sizeof(Run)) return false;
        const auto overlap = [](const void* a, size_t an, const void* b, size_t bn)
        {
            const auto x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
            return x <= y ? y - x < an : x - y < bn;
        };
        if (overlap(frame, capacity, run_buffer, sizeof(Run)) ||
            overlap(frame, capacity, root.shards.data, root.shards.size) ||
            overlap(run_buffer, sizeof(Run), root.shards.data, root.shards.size)) return false;
        root_ = root;
        frame_ = frame;
        capacity_ = capacity;
        run_ = new (run_buffer) Run;
        result_ = SortedIndexStep::Working;
        return true;
    }
    static const char* path(bool second)
    {
        return second ? "/trailmate/geocaching/.state/staging/sort-b.gci"
                      : "/trailmate/geocaching/.state/staging/sort-a.gci";
    }
    const char* sortedPath() const { return result_ == SortedIndexStep::Complete ? path(bank_) : nullptr; }
    uint64_t count() const { return total_; }
    SortedIndexStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != SortedIndexStep::Working) return result_;
        if (storage::sd_external_block_owner_active()) return SortedIndexStep::Busy;
        if (!storage::sd_card_ready()) return SortedIndexStep::Unavailable;
        // This is its own slice; a 512-byte source read must not be combined
        // with a volume-header transfer in the same public storage budget.
        if (check_volume_)
        {
            VolumeInstance current;
            const auto checked = inspectSdVolume(current);
            if (checked == SdVolumeResult::Unavailable) return SortedIndexStep::Unavailable;
            if (checked != SdVolumeResult::Ready) return fail(SortedIndexStep::IoError);
            if (current != volume_) return fail(SortedIndexStep::VolumeChanged);
            check_volume_ = false;
            return result_;
        }
        check_volume_ = true;
        switch (phase_)
        {
        case Phase::OpenRuns:
            if (!output_.open(path(false), "w")) return fail(SortedIndexStep::IoError);
            phase_ = Phase::Scan;
            break;
        case Phase::Scan:
        {
            if (table_ == 14)
            {
                if (used_) prepareRun();
                else phase_ = Phase::FlushRuns;
                break;
            }
            if (!scan_)
            {
                scan_.reset(new (std::nothrow) SdIndexScan(volume_));
                if (!scan_) return fail(SortedIndexStep::OutOfMemory);
                if (!scan_->begin(root_, table_, frame_, capacity_)) return fail(SortedIndexStep::Invalid);
            }
            const auto status = scan_->step();
            if (status == IndexScanStep::Working) break;
            if (status == IndexScanStep::End)
            {
                scan_.reset();
                ++table_;
                break;
            }
            if (status != IndexScanStep::Item) return fail(status == IndexScanStep::VolumeChanged ? SortedIndexStep::VolumeChanged
                                                           : status == IndexScanStep::IoError     ? SortedIndexStep::IoError
                                                                                                  : SortedIndexStep::Invalid);
            IndexedMutation item;
            if (!scan_->indexedItem(item) || !encodeIndexEntry(volume_, item, (*run_)[used_]) || !scan_->advance()) return fail(SortedIndexStep::Invalid);
            if (total_ == UINT32_MAX / kIndexEntrySize) return fail(SortedIndexStep::Invalid);
            ++total_;
            if (++used_ == run_->size()) prepareRun();
            break;
        }
        case Phase::WriteRun:
            if (output_.write((*run_)[written_].data(), kIndexEntrySize) != kIndexEntrySize) return fail(SortedIndexStep::IoError);
            if (++written_ == used_)
            {
                used_ = written_ = 0;
                phase_ = Phase::Scan;
            }
            break;
        case Phase::FlushRuns:
            if (!output_.flush()) return fail(SortedIndexStep::IoError);
            output_.close();
            if (total_ <= run_->size()) return fail(SortedIndexStep::Complete);
            phase_ = Phase::OpenMerge;
            break;
        case Phase::OpenMerge:
            if (!left_.open(path(bank_), "r") || !right_.open(path(bank_), "r") ||
                left_.size() != total_ * kIndexEntrySize || right_.size() != total_ * kIndexEntrySize ||
                !output_.open(path(!bank_), "w")) return fail(SortedIndexStep::IoError);
            pair_ = emitted_ = 0;
            phase_ = Phase::Pair;
            break;
        case Phase::Pair:
            if (pair_ == total_)
            {
                phase_ = Phase::FlushMerge;
                break;
            }
            left_pos_ = pair_;
            left_end_ = right_pos_ = std::min(total_, pair_ + width_);
            right_end_ = std::min(total_, left_end_ + width_);
            left_valid_ = right_valid_ = false;
            left_read_ = right_read_ = 0;
            phase_ = Phase::SeekLeft;
            break;
        case Phase::SeekLeft:
            if (!left_.seek(left_pos_ * kIndexEntrySize)) return fail(SortedIndexStep::IoError);
            phase_ = Phase::SeekRight;
            break;
        case Phase::SeekRight:
            if (!right_.seek(right_pos_ * kIndexEntrySize)) return fail(SortedIndexStep::IoError);
            phase_ = Phase::ReadLeft;
            break;
        case Phase::ReadLeft:
            if (!read(left_, left_bytes_, left_read_, left_valid_, left_pos_, left_end_)) return result_;
            phase_ = Phase::ReadRight;
            break;
        case Phase::ReadRight:
            if (!read(right_, right_bytes_, right_read_, right_valid_, right_pos_, right_end_)) return result_;
            phase_ = Phase::Pick;
            break;
        case Phase::Pick:
            if (!left_valid_ && !right_valid_)
            {
                pair_ = right_end_;
                phase_ = Phase::Pair;
                break;
            }
            if (left_valid_ && right_valid_ && !less(left_bytes_, right_bytes_) && !less(right_bytes_, left_bytes_)) return fail(SortedIndexStep::Invalid);
            take_left_ = left_valid_ && (!right_valid_ || less(left_bytes_, right_bytes_));
            phase_ = Phase::WriteMerge;
            break;
        case Phase::WriteMerge:
            if (output_.write((take_left_ ? left_bytes_ : right_bytes_).data(), kIndexEntrySize) != kIndexEntrySize) return fail(SortedIndexStep::IoError);
            ++emitted_;
            if (take_left_)
            {
                left_valid_ = false;
                left_read_ = 0;
            }
            else
            {
                right_valid_ = false;
                right_read_ = 0;
            }
            phase_ = Phase::ReadLeft;
            break;
        case Phase::FlushMerge:
            if (emitted_ != total_ || output_.size() != total_ * kIndexEntrySize) return fail(SortedIndexStep::Invalid);
            if (!output_.flush()) return fail(SortedIndexStep::IoError);
            output_.close();
            left_.close();
            right_.close();
            bank_ = !bank_;
            width_ = std::min(total_, width_ * 2);
            if (width_ == total_) return fail(SortedIndexStep::Complete);
            phase_ = Phase::OpenMerge;
            break;
        }
        return result_;
    }

  private:
    enum class Phase : uint8_t
    {
        OpenRuns,
        Scan,
        WriteRun,
        FlushRuns,
        OpenMerge,
        Pair,
        SeekLeft,
        SeekRight,
        ReadLeft,
        ReadRight,
        Pick,
        WriteMerge,
        FlushMerge
    };
    static bool less(const Entry& a, const Entry& b)
    {
        if (a[20] != b[20]) return a[20] < b[20];
        const auto length = std::min(a[21], b[21]);
        const int order = std::memcmp(a.data() + 52, b.data() + 52, length);
        return order < 0 || (!order && a[21] < b[21]);
    }
    void prepareRun()
    {
        std::sort(run_->begin(), run_->begin() + used_, less);
        written_ = 0;
        phase_ = Phase::WriteRun;
    }
    bool read(storage::SdRuntimeFile& file, Entry& bytes, uint16_t& read_bytes, bool& valid, uint64_t& position, uint64_t end)
    {
        if (valid || position == end) return true;
        const int count = file.read(bytes.data() + read_bytes, bytes.size() - read_bytes);
        if (count <= 0 || static_cast<size_t>(count) > bytes.size() - read_bytes)
        {
            fail(SortedIndexStep::IoError);
            return false;
        }
        read_bytes += static_cast<uint16_t>(count);
        if (read_bytes != bytes.size()) return false;
        ::geocaching::storage::IndexedMutation item;
        if (!::geocaching::storage::decodeIndexEntry({bytes.data(), bytes.size()}, volume_, item) || item.erase)
        {
            fail(SortedIndexStep::Invalid);
            return false;
        }
        valid = true;
        ++position;
        return true;
    }
    SortedIndexStep fail(SortedIndexStep result)
    {
        scan_.reset();
        left_.close();
        right_.close();
        output_.close();
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    std::unique_ptr<SdIndexScan> scan_;
    Run* run_ = nullptr;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, used_ = 0, written_ = 0;
    storage::SdRuntimeFile left_, right_, output_;
    Entry left_bytes_{}, right_bytes_{};
    uint64_t total_ = 0, width_ = 32, pair_ = 0, emitted_ = 0, left_pos_ = 0, left_end_ = 0, right_pos_ = 0, right_end_ = 0;
    uint16_t left_read_ = 0, right_read_ = 0;
    uint8_t table_ = 1;
    bool bank_ = false, left_valid_ = false, right_valid_ = false, take_left_ = false, check_volume_ = true;
    Phase phase_ = Phase::OpenRuns;
    SortedIndexStep result_ = SortedIndexStep::Idle;
};
static_assert(sizeof(SdSortedIndex) <= 1024, "External index sort retains two entries and cursors only");
} // namespace platform::esp::arduino_common::geocaching
