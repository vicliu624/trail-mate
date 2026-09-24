#pragma once
#include "geocaching/storage/index_shard_head.h"
#include "platform/esp/arduino_common/geocaching/sd_index_path.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"

namespace platform::esp::arduino_common::geocaching
{
enum class IndexHeadReadStep : uint8_t
{
    Idle,
    Working,
    Ready,
    Invalid,
    IoError,
    VolumeChanged
};

class SdIndexHeadReader
{
  public:
    SdIndexHeadReader(const ::geocaching::storage::VolumeInstance& volume, char slot, uint64_t epoch, uint64_t visible_sequence)
        : volume_(volume), epoch_(epoch), visible_(visible_sequence), slot_(slot) {}
    bool begin(uint8_t table, ::geocaching::ByteView key)
    {
        return key.data && key.size && key.size <= 96 && beginBucket(table, static_cast<uint8_t>(::sys::crc32(key.data, key.size)));
    }
    bool beginBucket(uint8_t table, uint8_t bucket)
    {
        if (result_ != IndexHeadReadStep::Idle || !epoch_ || !indexShardHeadPathForBucket(slot_, table, bucket, 0, path_.data(), path_.size())) return false;
        table_ = table;
        bucket_ = bucket;
        result_ = IndexHeadReadStep::Working;
        return true;
    }
    bool selected(::geocaching::storage::IndexShardHead& out) const
    {
        out = {};
        if (result_ != IndexHeadReadStep::Ready) return false;
        out = selected_;
        return true;
    }
    int selectedCopy() const
    {
        if (result_ != IndexHeadReadStep::Ready) return -1;
        return selected_.sequence == heads_[0].sequence ? 0 : 1;
    }
    IndexHeadReadStep step()
    {
        if (result_ != IndexHeadReadStep::Working) return result_;
        if (phase_ == 0 || phase_ == 3)
        {
            ::geocaching::storage::VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return result_ = IndexHeadReadStep::IoError;
            if (current != volume_) return result_ = IndexHeadReadStep::VolumeChanged;
            if (phase_ == 3) return result_ = IndexHeadReadStep::Ready;
            phase_ = 1;
            return result_;
        }
        const unsigned copy = phase_ - 1;
        path_[std::strlen(path_.data()) - 1] = static_cast<char>('0' + copy);
        const auto read = storage::sd_read_file(path_.data(), bytes_.data(), bytes_.size());
        if (read.status == storage::SdFileReadStatus::Missing || read.status == storage::SdFileReadStatus::Invalid)
            return result_ = IndexHeadReadStep::Invalid;
        if (read.status != storage::SdFileReadStatus::Ready) return result_ = IndexHeadReadStep::IoError;
        if (read.file_size != bytes_.size() || read.bytes_read != bytes_.size() ||
            !::geocaching::storage::decodeIndexShardHead({bytes_.data(), bytes_.size()}, volume_, epoch_, table_, bucket_, heads_[copy]))
            return result_ = IndexHeadReadStep::Invalid;
        if (copy == 1 && !::geocaching::storage::selectIndexShardHead(heads_[0], heads_[1], visible_, selected_))
            return result_ = IndexHeadReadStep::Invalid;
        ++phase_;
        return result_;
    }

  private:
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexShardHeadBytes bytes_{};
    std::array<::geocaching::storage::IndexShardHead, 2> heads_{};
    ::geocaching::storage::IndexShardHead selected_;
    std::array<char, 80> path_{};
    uint64_t epoch_, visible_;
    char slot_;
    uint8_t table_ = 0, bucket_ = 0, phase_ = 0;
    IndexHeadReadStep result_ = IndexHeadReadStep::Idle;
};
static_assert(sizeof(SdIndexHeadReader) <= 288, "Shard head readers retain metadata only");
} // namespace platform::esp::arduino_common::geocaching
