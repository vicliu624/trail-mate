#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_get.h"
#include "platform/esp/arduino_common/geocaching/sd_index_scan.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
// Exclusive maintenance owner pins both roots and two disjoint frame leases.
// Compare both directions: a subset is not an equivalent checkpoint. At most
// one live row from each side is retained, independent of ledger size.
class SdIndexEquivalent
{
  public:
    explicit SdIndexEquivalent(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& first,
               const ::geocaching::storage::IndexRootView& second,
               uint8_t* first_frame, size_t first_capacity, uint8_t* second_frame, size_t second_capacity)
    {
        using ::geocaching::storage::validIndexRoot;
        if (result_ != IndexScanStep::Idle || !validIndexRoot(first) || !validIndexRoot(second) ||
            first.sequence != second.sequence || !first_frame || !second_frame || first_capacity < 24 || second_capacity < 24) return false;
        const auto overlap = [](::geocaching::ByteView a, ::geocaching::ByteView b)
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data), y = reinterpret_cast<uintptr_t>(b.data);
            return x <= y ? y - x < a.size : x - y < b.size;
        };
        const ::geocaching::ByteView a{first_frame, first_capacity}, b{second_frame, second_capacity};
        if (overlap(a, b) || overlap(a, first.shards) || overlap(a, second.shards) ||
            overlap(b, first.shards) || overlap(b, second.shards) ||
            overlap(a, {reinterpret_cast<const uint8_t*>(this), sizeof(*this)}) ||
            overlap(b, {reinterpret_cast<const uint8_t*>(this), sizeof(*this)})) return false;
        roots_[0] = first;
        roots_[1] = second;
        frames_[0] = first_frame;
        frames_[1] = second_frame;
        capacities_[0] = first_capacity;
        capacities_[1] = second_capacity;
        result_ = IndexScanStep::Working;
        return true;
    }
    IndexScanStep step()
    {
        if (result_ != IndexScanStep::Working) return result_;
        if (!scan_)
        {
            if (direction_ == 2) return result_ = IndexScanStep::End;
            scan_.emplace(volume_);
            if (!scan_->begin(roots_[direction_], table_, frames_[0], capacities_[0])) return fail(IndexScanStep::Invalid);
            return result_;
        }
        if (get_)
        {
            const auto status = get_->step();
            if (status == IndexGetStep::Working) return result_;
            if (status != IndexGetStep::Ready)
                return fail(status == IndexGetStep::IoError ? IndexScanStep::IoError : status == IndexGetStep::VolumeChanged   ? IndexScanStep::VolumeChanged
                                                                                   : status == IndexGetStep::WorkspaceTooSmall ? IndexScanStep::WorkspaceTooSmall
                                                                                                                               : IndexScanStep::Invalid);
            ::geocaching::storage::MutationView row;
            const auto other = get_->value();
            if (!scan_->item(row) || row.value.size != other.size ||
                (other.size && std::memcmp(row.value.data, other.data, other.size))) return fail(IndexScanStep::Invalid);
            get_.reset();
            if (!scan_->advance()) return fail(IndexScanStep::Invalid);
            return result_;
        }
        const auto status = scan_->step();
        if (status == IndexScanStep::Working) return result_;
        if (status == IndexScanStep::Item)
        {
            ::geocaching::storage::MutationView row;
            if (!scan_->item(row)) return fail(IndexScanStep::Invalid);
            get_.emplace(volume_);
            if (!get_->begin(roots_[1 - direction_], table_, row.key, frames_[1], capacities_[1])) return fail(IndexScanStep::Invalid);
            return result_;
        }
        if (status != IndexScanStep::End) return fail(status);
        scan_.reset();
        if (++table_ == 14)
        {
            table_ = 1;
            ++direction_;
        }
        return result_;
    }

  private:
    IndexScanStep fail(IndexScanStep status)
    {
        get_.reset();
        scan_.reset();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView roots_[2];
    uint8_t* frames_[2] = {};
    size_t capacities_[2] = {};
    std::optional<SdIndexScan> scan_;
    std::optional<SdIndexGet> get_;
    uint8_t table_ = 1, direction_ = 0;
    IndexScanStep result_ = IndexScanStep::Idle;
};
} // namespace platform::esp::arduino_common::geocaching
