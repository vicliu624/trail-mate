#pragma once
#include "geocaching/storage/checkpoint_index_cursor.h"
#include "geocaching/storage/index_entry.h"
#include "platform/esp/arduino_common/geocaching/sd_record_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"
#include <cstdio>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexedReadStep : uint8_t
{
    Idle,
    Working,
    Ready,
    Erased,
    Invalid,
    IoError,
    VolumeChanged,
    WorkspaceTooSmall
};

// One immutable value lease. Caller owns the frame buffer through consumption;
// each step performs at most 512 data bytes of I/O. The index is only a hint.
class SdIndexedValueReader
{
  public:
    explicit SdIndexedValueReader(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexedMutation& entry, uint8_t* frame, size_t capacity)
    {
        if (result_ != IndexedReadStep::Idle || !::geocaching::storage::validIndexEntry(entry) || !entry.key.data || !entry.key.size || entry.key.size > key_.size() ||
            entry.table < 1 || entry.table > 13 || !entry.location.record_sequence || !entry.location.segment_first_sequence ||
            entry.location.segment_first_sequence > entry.location.record_sequence || !frame || capacity < 24) return false;
        table_ = entry.table;
        key_size_ = entry.key.size;
        erase_ = entry.erase;
        location_ = entry.location;
        std::memcpy(key_.data(), entry.key.data, key_size_);
        bytes_ = frame;
        capacity_ = capacity;
        result_ = IndexedReadStep::Working;
        return true;
    }
    ::geocaching::ByteView value() const { return result_ == IndexedReadStep::Ready ? value_ : ::geocaching::ByteView{}; }
    IndexedReadStep step()
    {
        if (result_ != IndexedReadStep::Working) return result_;
        if (phase_ == Phase::Volume || phase_ == Phase::VerifyVolume)
        {
            ::geocaching::storage::VolumeInstance current;
            const auto status = inspectSdVolume(current);
            if (status != SdVolumeResult::Ready) return fail(IndexedReadStep::IoError);
            if (current != volume_) return fail(IndexedReadStep::VolumeChanged);
            if (phase_ == Phase::VerifyVolume) return result_ = erase_ ? IndexedReadStep::Erased : IndexedReadStep::Ready;
            phase_ = Phase::Open;
            return result_;
        }
        if (phase_ == Phase::Open)
        {
            char path[80];
            if (location_.source == ::geocaching::storage::IndexedValueSource::Journal)
                std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/journal/%016llx.gcj",
                              static_cast<unsigned long long>(location_.segment_first_sequence));
            else std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/checkpoint/%c.gcs",
                               location_.source == ::geocaching::storage::IndexedValueSource::CheckpointA ? 'a' : 'b');
            if (!file_.open(path, "r")) return fail(IndexedReadStep::IoError);
            phase_ = Phase::Size;
            return result_;
        }
        if (phase_ == Phase::Size)
        {
            length_ = file_.size();
            const uint64_t limit = location_.source == ::geocaching::storage::IndexedValueSource::Journal ? 1024U * 1024U : UINT32_MAX;
            if (!length_ || length_ > limit || location_.frame_offset >= length_) return fail(IndexedReadStep::Invalid);
            phase_ = Phase::Seek;
            return result_;
        }
        if (phase_ == Phase::Seek)
        {
            if (!file_.seek(location_.frame_offset)) return fail(IndexedReadStep::IoError);
            offset_ = location_.frame_offset;
            phase_ = Phase::Read;
            return result_;
        }
        if (phase_ == Phase::Read)
        {
            ::geocaching::storage::RecordFrameView frame;
            const auto read = readSdRecord(file_, length_, offset_, bytes_, capacity_, read_cursor_, frame);
            if (read == SegmentReadResult::InProgress) return result_;
            if (read == SegmentReadResult::WorkspaceTooSmall) return fail(IndexedReadStep::WorkspaceTooSmall);
            if (read == SegmentReadResult::IoError) return fail(IndexedReadStep::IoError);
            if (read != SegmentReadResult::Record) return fail(IndexedReadStep::Invalid);
            frame_size_ = frame.payload.size + 24;
            phase_ = Phase::Validate;
            return result_;
        }
        if (phase_ == Phase::Validate)
        {
            auto locate = [&](auto& cursor)
            {
                ::geocaching::storage::IndexedMutation entry;
                while (cursor.next(entry))
                {
                    if (entry.table != table_ || entry.key.size != key_size_ || std::memcmp(entry.key.data, key_.data(), key_size_)) continue;
                    if (entry.erase != erase_ || entry.location.value_offset != location_.value_offset || entry.location.value_size != location_.value_size) return false;
                    if (!erase_) value_ = {bytes_ + (entry.location.value_offset - location_.frame_offset), entry.location.value_size};
                    return true;
                }
                return false;
            };
            bool found = false;
            if (location_.source == ::geocaching::storage::IndexedValueSource::Journal)
            {
                ::geocaching::storage::TransactionIndexCursor cursor;
                found = cursor.open({bytes_, frame_size_}, location_.record_sequence - 1, location_.segment_first_sequence, location_.frame_offset) && locate(cursor);
            }
            else
            {
                ::geocaching::storage::CheckpointIndexCursor cursor;
                found = cursor.open({bytes_, frame_size_}, location_.record_sequence,
                                    location_.source == ::geocaching::storage::IndexedValueSource::CheckpointA ? 'a' : 'b', location_.frame_offset) &&
                        locate(cursor);
            }
            if (!found) return fail(IndexedReadStep::Invalid);
            phase_ = Phase::CheckSize;
            return result_;
        }
        if (phase_ == Phase::CheckSize)
        {
            if (file_.size() != length_) return fail(IndexedReadStep::Invalid);
            phase_ = Phase::Close;
            return result_;
        }
        file_.close();
        phase_ = Phase::VerifyVolume;
        return result_;
    }

  private:
    enum class Phase : uint8_t
    {
        Volume,
        Open,
        Size,
        Seek,
        Read,
        Validate,
        CheckSize,
        Close,
        VerifyVolume
    };
    IndexedReadStep fail(IndexedReadStep result)
    {
        file_.close();
        value_ = {};
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::JournalValueLocation location_;
    std::array<uint8_t, 96> key_{};
    size_t key_size_ = 0, capacity_ = 0, frame_size_ = 0;
    uint8_t table_ = 0;
    bool erase_ = false;
    uint8_t* bytes_ = nullptr;
    uint64_t length_ = 0, offset_ = 0;
    ::geocaching::ByteView value_;
    storage::SdRuntimeFile file_;
    SdRecordReadCursor read_cursor_;
    Phase phase_ = Phase::Volume;
    IndexedReadStep result_ = IndexedReadStep::Idle;
};
static_assert(sizeof(SdIndexedValueReader) <= 320, "Indexed reads must not own a frame buffer");
} // namespace platform::esp::arduino_common::geocaching
