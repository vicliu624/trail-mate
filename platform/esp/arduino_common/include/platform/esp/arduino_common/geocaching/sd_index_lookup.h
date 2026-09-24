#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_path.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"

namespace platform::esp::arduino_common::geocaching
{
enum class IndexLookupStep : uint8_t
{
    Idle,
    Working,
    Found,
    NotFound,
    Invalid,
    IoError,
    VolumeChanged
};

// Owner supplies the sequence/length of the selected, validated IndexShardHead,
// not the global journal watermark. Missing
// expected shards are invalid, not proof of absence. Returned locations remain
// hints and must pass SdIndexedValueReader before a value is trusted.
class SdIndexLookup
{
  public:
    SdIndexLookup(const ::geocaching::storage::VolumeInstance& volume, char slot, uint64_t committed_sequence, uint64_t committed_length)
        : volume_(volume), visible_(committed_sequence), position_(committed_length), length_(committed_length), slot_(slot) {}
    bool begin(uint8_t table, ::geocaching::ByteView key)
    {
        if (result_ != IndexLookupStep::Idle || (slot_ != 'a' && slot_ != 'b') || table < 1 || table > 13 ||
            !key.data || !key.size || key.size > key_.size() || length_ % ::geocaching::storage::kIndexEntrySize ||
            ((visible_ == 0) != (length_ == 0))) return false;
        table_ = table;
        key_size_ = key.size;
        std::memcpy(key_.data(), key.data, key_size_);
        result_ = IndexLookupStep::Working;
        return true;
    }
    bool result(::geocaching::storage::IndexedMutation& out) const
    {
        out = {};
        if (result_ != IndexLookupStep::Found) return false;
        out = {table_, {key_.data(), key_size_}, location_, erased_};
        return true;
    }
    uint64_t entryOffset() const { return result_ == IndexLookupStep::Found ? position_ : UINT64_MAX; }
    IndexLookupStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexLookupStep::Working) return result_;
        if (phase_ == Phase::Volume || phase_ == Phase::VerifyVolume)
        {
            VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexLookupStep::IoError);
            if (current != volume_) return fail(IndexLookupStep::VolumeChanged);
            if (phase_ == Phase::VerifyVolume) return result_ = completion_;
            phase_ = length_ ? Phase::Probe : Phase::VerifyVolume;
            return result_;
        }
        if (phase_ == Phase::Probe || phase_ == Phase::Open)
        {
            char path[80];
            if (!indexShardPath(slot_, table_, {key_.data(), key_size_}, path, sizeof(path))) return fail(IndexLookupStep::Invalid);
            if (phase_ == Phase::Probe)
            {
                const auto probe = storage::sd_read_file(path, bytes_.data(), 1);
                if (probe.status == storage::SdFileReadStatus::Missing) return fail(IndexLookupStep::Invalid);
                if (probe.status != storage::SdFileReadStatus::Ready && probe.status != storage::SdFileReadStatus::Invalid) return fail(IndexLookupStep::IoError);
                if (probe.file_size < length_) return fail(IndexLookupStep::Invalid);
                phase_ = Phase::Open;
                return result_;
            }
            if (!file_.open(path, "r")) return fail(IndexLookupStep::IoError);
            phase_ = Phase::Seek;
            return result_;
        }
        if (phase_ == Phase::Seek)
        {
            if (!position_)
            {
                completion_ = IndexLookupStep::NotFound;
                phase_ = Phase::CheckSize;
                return result_;
            }
            position_ -= kIndexEntrySize;
            if (!file_.seek(position_)) return fail(IndexLookupStep::IoError);
            read_ = 0;
            phase_ = Phase::Read;
            return result_;
        }
        if (phase_ == Phase::Read)
        {
            const int count = file_.read(bytes_.data() + read_, bytes_.size() - read_);
            if (count < 0) return fail(IndexLookupStep::IoError);
            if (!count || static_cast<size_t>(count) > bytes_.size() - read_) return fail(IndexLookupStep::Invalid);
            read_ += static_cast<uint16_t>(count);
            if (read_ != bytes_.size()) return result_;
            IndexedMutation entry;
            if (!decodeIndexEntry({bytes_.data(), bytes_.size()}, volume_, entry) || entry.location.record_sequence > newer_sequence_ ||
                (first_entry_ && entry.location.record_sequence != visible_) ||
                entry.table != table_ || (::sys::crc32(entry.key.data, entry.key.size) & 0xff) != (::sys::crc32(key_.data(), key_size_) & 0xff))
                return fail(IndexLookupStep::Invalid);
            newer_sequence_ = entry.location.record_sequence;
            first_entry_ = false;
            if (entry.location.record_sequence <= visible_ && entry.table == table_ && entry.key.size == key_size_ &&
                !std::memcmp(entry.key.data, key_.data(), key_size_))
            {
                location_ = entry.location;
                erased_ = entry.erase;
                completion_ = IndexLookupStep::Found;
                phase_ = Phase::CheckSize;
            }
            else phase_ = Phase::Seek;
            return result_;
        }
        if (phase_ == Phase::CheckSize)
        {
            if (file_.size() < length_) return fail(IndexLookupStep::Invalid);
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
        Probe,
        Open,
        Seek,
        Read,
        CheckSize,
        Close,
        VerifyVolume
    };
    IndexLookupStep fail(IndexLookupStep value)
    {
        file_.close();
        return result_ = value;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexEntryBytes bytes_{};
    std::array<uint8_t, 96> key_{};
    ::geocaching::storage::JournalValueLocation location_;
    uint64_t visible_, position_ = 0, length_ = 0, newer_sequence_ = UINT64_MAX;
    storage::SdRuntimeFile file_;
    size_t key_size_ = 0;
    uint16_t read_ = 0;
    char slot_;
    uint8_t table_ = 0;
    bool erased_ = false;
    bool first_entry_ = true;
    Phase phase_ = Phase::Volume;
    IndexLookupStep result_ = IndexLookupStep::Idle, completion_ = IndexLookupStep::NotFound;
};
static_assert(sizeof(SdIndexLookup) <= 384, "Index lookup must not load a whole shard");
} // namespace platform::esp::arduino_common::geocaching
