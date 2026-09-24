#pragma once
#include "geocaching/storage/index_root.h"
#include "platform/esp/arduino_common/geocaching/sd_index_head_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_index_lookup.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_value_reader.h"
#include <variant>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexGetStep : uint8_t
{
    Idle,
    Working,
    Ready,
    NotFound,
    Invalid,
    IoError,
    VolumeChanged,
    WorkspaceTooSmall
};

// The owner pins a validated root for the read and supplies a disjoint frame
// lease. Operation workspaces overlap; neither values nor the full index live
// in this object. Keep it in owner storage, not an ESP task-stack local.
class SdIndexGet
{
  public:
    explicit SdIndexGet(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, uint8_t table, ::geocaching::ByteView key,
               uint8_t* frame, size_t capacity)
    {
        if (result_ != IndexGetStep::Idle || !::geocaching::storage::validIndexRoot(root) || table < 1 || table > 13 ||
            !key.data || !key.size || key.size > key_.size() || !frame || capacity < 24) return false;
        const auto input = reinterpret_cast<uintptr_t>(root.shards.data), output = reinterpret_cast<uintptr_t>(frame);
        if (input <= output ? output - input < root.shards.size : input - output < capacity) return false;
        root_ = root;
        table_ = table;
        key_size_ = key.size;
        std::memcpy(key_.data(), key.data, key_size_);
        frame_ = frame;
        capacity_ = capacity;
        const auto bucket = static_cast<uint8_t>(::sys::crc32(key_.data(), key_size_));
        if (::geocaching::storage::indexHasShard(root_, table_, bucket))
        {
            if (!operation_.emplace<SdIndexHeadReader>(volume_, root_.slot, root_.epoch, root_.sequence).begin(table_, {key_.data(), key_size_})) return false;
            phase_ = Phase::Head;
        }
        result_ = IndexGetStep::Working;
        return true;
    }
    ::geocaching::ByteView value() const { return result_ == IndexGetStep::Ready ? value_ : ::geocaching::ByteView{}; }
    IndexGetStep step()
    {
        if (result_ != IndexGetStep::Working) return result_;
        if (phase_ == Phase::Absent)
        {
            ::geocaching::storage::VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexGetStep::IoError);
            return fail(current == volume_ ? IndexGetStep::NotFound : IndexGetStep::VolumeChanged);
        }
        if (phase_ == Phase::Head)
        {
            auto& reader = std::get<SdIndexHeadReader>(operation_);
            const auto status = reader.step();
            if (status == IndexHeadReadStep::Working) return result_;
            ::geocaching::storage::IndexShardHead head;
            if (status != IndexHeadReadStep::Ready || !reader.selected(head)) return error(status);
            if (!operation_.emplace<SdIndexLookup>(volume_, root_.slot, head.sequence, head.length).begin(table_, {key_.data(), key_size_})) return fail(IndexGetStep::Invalid);
            phase_ = Phase::Lookup;
            return result_;
        }
        if (phase_ == Phase::Lookup)
        {
            auto& lookup = std::get<SdIndexLookup>(operation_);
            const auto status = lookup.step();
            if (status == IndexLookupStep::Working) return result_;
            if (status == IndexLookupStep::NotFound) return fail(IndexGetStep::NotFound);
            ::geocaching::storage::IndexedMutation hint;
            if (status != IndexLookupStep::Found || !lookup.result(hint)) return error(status);
            // The lookup workspace is about to be destroyed; pin the key in
            // this owner before constructing the next operation in its place.
            hint.key = {key_.data(), key_size_};
            if (!operation_.emplace<SdIndexedValueReader>(volume_).begin(hint, frame_, capacity_)) return fail(IndexGetStep::Invalid);
            phase_ = Phase::Value;
            return result_;
        }
        auto& reader = std::get<SdIndexedValueReader>(operation_);
        const auto status = reader.step();
        if (status == IndexedReadStep::Working) return result_;
        if (status == IndexedReadStep::WorkspaceTooSmall) return fail(IndexGetStep::WorkspaceTooSmall);
        if (status == IndexedReadStep::Erased) return fail(IndexGetStep::NotFound);
        if (status != IndexedReadStep::Ready) return error(status);
        value_ = reader.value();
        result_ = IndexGetStep::Ready;
        return result_;
    }

  private:
    enum class Phase : uint8_t
    {
        Absent,
        Head,
        Lookup,
        Value
    };
    template <class Status>
    IndexGetStep error(Status status)
    {
        return fail(status == Status::VolumeChanged ? IndexGetStep::VolumeChanged : status == Status::IoError ? IndexGetStep::IoError
                                                                                                              : IndexGetStep::Invalid);
    }
    IndexGetStep fail(IndexGetStep result)
    {
        operation_.emplace<std::monostate>();
        value_ = {};
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    std::array<uint8_t, 96> key_{};
    size_t key_size_ = 0, capacity_ = 0;
    uint8_t* frame_ = nullptr;
    ::geocaching::ByteView value_;
    std::variant<std::monostate, SdIndexHeadReader, SdIndexLookup, SdIndexedValueReader> operation_;
    uint8_t table_ = 0;
    Phase phase_ = Phase::Absent;
    IndexGetStep result_ = IndexGetStep::Idle;
};
static_assert(sizeof(SdIndexGet) <= 640, "Indexed gets must overlay their metadata workspaces");
} // namespace platform::esp::arduino_common::geocaching
