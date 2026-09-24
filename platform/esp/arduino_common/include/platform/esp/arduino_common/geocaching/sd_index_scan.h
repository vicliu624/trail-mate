#pragma once
#include "geocaching/storage/index_root.h"
#include "geocaching/storage/record_shape.h"
#include "platform/esp/arduino_common/geocaching/sd_index_head_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_index_lookup.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_value_reader.h"
#include <variant>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexScanStep : uint8_t
{
    Idle,
    Working,
    Item,
    End,
    Invalid,
    IoError,
    VolumeChanged,
    WorkspaceTooSmall
};

// Snapshot and frame leases are pinned through the scan. An Item borrows both
// the key and value until advance(). Owner storage only, never an ESP stack local.
class SdIndexScan
{
  public:
    explicit SdIndexScan(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, uint8_t table, uint8_t* frame, size_t capacity)
    {
        if (result_ != IndexScanStep::Idle || !::geocaching::storage::validIndexRoot(root) || table < 1 || table > 13 || !frame || capacity < 24) return false;
        const auto a = reinterpret_cast<uintptr_t>(root.shards.data), b = reinterpret_cast<uintptr_t>(frame);
        if (a <= b ? b - a < root.shards.size : a - b < capacity) return false;
        root_ = root;
        table_ = table;
        frame_ = frame;
        capacity_ = capacity;
        result_ = IndexScanStep::Working;
        return true;
    }
    bool item(::geocaching::storage::MutationView& out) const
    {
        out = {};
        if (result_ != IndexScanStep::Item) return false;
        out = {table_, entry_.key, value_, false};
        return true;
    }
    bool advance()
    {
        if (result_ != IndexScanStep::Item) return false;
        value_ = {};
        result_ = IndexScanStep::Working;
        return true;
    }
    IndexScanStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexScanStep::Working) return result_;
        if (phase_ == Phase::Bucket)
        {
            while (bucket_ < 256 && !indexHasShard(root_, table_, static_cast<uint8_t>(bucket_))) ++bucket_;
            if (bucket_ == 256)
            {
                phase_ = Phase::Finish;
                return result_;
            }
            if (!operation_.emplace<SdIndexHeadReader>(volume_, root_.slot, root_.epoch, root_.sequence).beginBucket(table_, static_cast<uint8_t>(bucket_))) return fail(IndexScanStep::Invalid);
            phase_ = Phase::Head;
            return result_;
        }
        if (phase_ == Phase::Head)
        {
            auto& reader = std::get<SdIndexHeadReader>(operation_);
            const auto status = reader.step();
            if (status == IndexHeadReadStep::Working) return result_;
            if (status != IndexHeadReadStep::Ready || !reader.selected(head_) || !head_.length) return error(status);
            operation_.emplace<std::monostate>();
            phase_ = Phase::Open;
            return result_;
        }
        if (phase_ == Phase::Open)
        {
            char path[80];
            if (!indexShardPathForBucket(root_.slot, table_, static_cast<uint8_t>(bucket_), path, sizeof(path)) || !file_.open(path, "r")) return fail(IndexScanStep::IoError);
            phase_ = Phase::Size;
            return result_;
        }
        if (phase_ == Phase::Size)
        {
            if (file_.size() < head_.length) return fail(IndexScanStep::Invalid);
            position_ = head_.length;
            newer_ = UINT64_MAX;
            phase_ = Phase::Seek;
            return result_;
        }
        if (phase_ == Phase::Seek)
        {
            if (!position_)
            {
                phase_ = Phase::EndShard;
                return result_;
            }
            position_ -= kIndexEntrySize;
            if (!file_.seek(position_)) return fail(IndexScanStep::IoError);
            read_ = 0;
            phase_ = Phase::Read;
            return result_;
        }
        if (phase_ == Phase::Read)
        {
            const int count = file_.read(bytes_.data() + read_, bytes_.size() - read_);
            if (count < 0) return fail(IndexScanStep::IoError);
            if (!count || static_cast<size_t>(count) > bytes_.size() - read_) return fail(IndexScanStep::Invalid);
            read_ += static_cast<uint16_t>(count);
            if (read_ != bytes_.size()) return result_;
            if (!decodeIndexEntry({bytes_.data(), bytes_.size()}, volume_, entry_) || entry_.table != table_ ||
                static_cast<uint8_t>(::sys::crc32(entry_.key.data, entry_.key.size)) != bucket_ || entry_.location.record_sequence > newer_ ||
                (position_ == head_.length - kIndexEntrySize && entry_.location.record_sequence != head_.sequence)) return fail(IndexScanStep::Invalid);
            newer_ = entry_.location.record_sequence;
            if (!operation_.emplace<SdIndexLookup>(volume_, root_.slot, head_.sequence, head_.length).begin(table_, entry_.key)) return fail(IndexScanStep::Invalid);
            phase_ = Phase::Latest;
            return result_;
        }
        if (phase_ == Phase::Latest)
        {
            auto& lookup = std::get<SdIndexLookup>(operation_);
            const auto status = lookup.step();
            if (status == IndexLookupStep::Working) return result_;
            if (status != IndexLookupStep::Found) return error(status);
            if (lookup.entryOffset() != position_)
            {
                operation_.emplace<std::monostate>();
                phase_ = Phase::Seek;
                return result_;
            }
            if (!operation_.emplace<SdIndexedValueReader>(volume_).begin(entry_, frame_, capacity_)) return fail(IndexScanStep::Invalid);
            phase_ = Phase::Value;
            return result_;
        }
        if (phase_ == Phase::Value)
        {
            auto& reader = std::get<SdIndexedValueReader>(operation_);
            const auto status = reader.step();
            if (status == IndexedReadStep::Working) return result_;
            if (status == IndexedReadStep::WorkspaceTooSmall) return fail(IndexScanStep::WorkspaceTooSmall);
            if (status != IndexedReadStep::Ready && status != IndexedReadStep::Erased) return error(status);
            value_ = reader.value();
            operation_.emplace<std::monostate>();
            phase_ = Phase::Seek;
            if (status == IndexedReadStep::Erased) return result_;
            if (!validStoredRowShape({table_, entry_.key, value_, false})) return fail(IndexScanStep::Invalid);
            return result_ = IndexScanStep::Item;
        }
        if (phase_ == Phase::EndShard)
        {
            if (file_.size() < head_.length) return fail(IndexScanStep::Invalid);
            file_.close();
            ++bucket_;
            phase_ = Phase::Bucket;
            return result_;
        }
        ::geocaching::storage::VolumeInstance current;
        if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexScanStep::IoError);
        return fail(current == volume_ ? IndexScanStep::End : IndexScanStep::VolumeChanged);
    }

  private:
    enum class Phase : uint8_t
    {
        Bucket,
        Head,
        Open,
        Size,
        Seek,
        Read,
        Latest,
        Value,
        EndShard,
        Finish
    };
    template <class Status>
    IndexScanStep error(Status status)
    {
        return fail(status == Status::VolumeChanged ? IndexScanStep::VolumeChanged : status == Status::IoError ? IndexScanStep::IoError
                                                                                                               : IndexScanStep::Invalid);
    }
    IndexScanStep fail(IndexScanStep status)
    {
        file_.close();
        operation_.emplace<std::monostate>();
        value_ = {};
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::IndexShardHead head_;
    ::geocaching::storage::IndexEntryBytes bytes_{};
    ::geocaching::storage::IndexedMutation entry_;
    ::geocaching::ByteView value_;
    storage::SdRuntimeFile file_;
    std::variant<std::monostate, SdIndexHeadReader, SdIndexLookup, SdIndexedValueReader> operation_;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    uint64_t position_ = 0, newer_ = 0;
    uint16_t bucket_ = 0, read_ = 0;
    uint8_t table_ = 0;
    Phase phase_ = Phase::Bucket;
    IndexScanStep result_ = IndexScanStep::Idle;
};
static_assert(sizeof(SdIndexScan) <= 832, "Table scans retain one current row, not a table");
} // namespace platform::esp::arduino_common::geocaching
