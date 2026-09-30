#pragma once
#include "geocaching/storage/index_shard_head.h"
#include "platform/esp/arduino_common/geocaching/sd_index_path.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"

namespace platform::esp::arduino_common::geocaching
{
enum class IndexHeadWriteStep : uint8_t
{
    Idle,
    Working,
    Verified,
    Invalid,
    IoError,
    VolumeChanged
};

// Owner chooses the inactive copy after reading BOTH heads, or initializes
// both empty copies in an unpublished generation. This does not publish a
// global watermark and must never overwrite the only committed boundary.
class SdIndexHeadWriter
{
  public:
    SdIndexHeadWriter(const ::geocaching::storage::VolumeInstance& volume, char slot) : volume_(volume), slot_(slot) {}
    bool begin(::geocaching::ByteView key, unsigned copy, const ::geocaching::storage::IndexShardHead& head)
    {
        if (result_ != IndexHeadWriteStep::Idle || !key.data || !key.size || key.size > 96 ||
            head.bucket != static_cast<uint8_t>(::sys::crc32(key.data, key.size)) ||
            !indexShardHeadPath(slot_, head.table, key, copy, path_.data(), path_.size()) ||
            !::geocaching::storage::encodeIndexShardHead(volume_, head, bytes_)) return false;
        result_ = IndexHeadWriteStep::Working;
        return true;
    }
    IndexHeadWriteStep step()
    {
        if (result_ != IndexHeadWriteStep::Working) return result_;
        if (phase_ == Phase::Volume || phase_ == Phase::VerifyVolume)
        {
            ::geocaching::storage::VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexHeadWriteStep::IoError);
            if (current != volume_) return fail(IndexHeadWriteStep::VolumeChanged);
            if (phase_ == Phase::VerifyVolume) return result_ = IndexHeadWriteStep::Verified;
            phase_ = Phase::Open;
            return result_;
        }
        if (phase_ == Phase::Open)
        {
            if (!file_.open(path_.data(), "w")) return fail(IndexHeadWriteStep::IoError);
            phase_ = Phase::Write;
            return result_;
        }
        if (phase_ == Phase::Write)
        {
            if (file_.write(bytes_.data(), bytes_.size()) != bytes_.size()) return fail(IndexHeadWriteStep::IoError);
            phase_ = Phase::Flush;
            return result_;
        }
        if (phase_ == Phase::Flush)
        {
            if (!file_.flush()) return fail(IndexHeadWriteStep::IoError);
            phase_ = Phase::Close;
            return result_;
        }
        if (phase_ == Phase::Close)
        {
            file_.close();
            phase_ = Phase::Readback;
            return result_;
        }
        const auto read = storage::sd_read_file(path_.data(), verify_.data(), verify_.size());
        if (read.status != storage::SdFileReadStatus::Ready) return fail(IndexHeadWriteStep::IoError);
        if (read.file_size != bytes_.size() || read.bytes_read != bytes_.size() || verify_ != bytes_) return fail(IndexHeadWriteStep::Invalid);
        phase_ = Phase::VerifyVolume;
        return result_;
    }

  private:
    enum class Phase : uint8_t
    {
        Volume,
        Open,
        Write,
        Flush,
        Close,
        Readback,
        VerifyVolume
    };
    IndexHeadWriteStep fail(IndexHeadWriteStep result)
    {
        file_.close();
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexShardHeadBytes bytes_{}, verify_{};
    std::array<char, 80> path_{};
    storage::SdRuntimeFile file_;
    char slot_;
    Phase phase_ = Phase::Volume;
    IndexHeadWriteStep result_ = IndexHeadWriteStep::Idle;
};
static_assert(sizeof(SdIndexHeadWriter) <= 224, "Boundary writes retain metadata only");
} // namespace platform::esp::arduino_common::geocaching
