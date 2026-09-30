#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_root_writer.h"
#include <optional>

namespace platform::esp::arduino_common::geocaching
{
// Creates an empty derived index only when no index directory exists. It does
// not declare the authoritative journal empty: the owner must replay it before
// admitting requests. Interrupted initialization requires explicit recovery.
class SdIndexInitialize
{
  public:
    explicit SdIndexInitialize(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(::geocaching::storage::IndexRootBytes& bytes)
    {
        if (result_ != IndexRootWriteStep::Idle) return false;
        bytes.fill(0);
        const ::geocaching::storage::IndexRootView empty{1, 0, 1, 'a', {bytes.data() + 48, ::geocaching::storage::kIndexShardBitmapSize}};
        if (!::geocaching::storage::encodeIndexRoot(volume_, empty, bytes)) return false;
        bytes_ = &bytes;
        result_ = IndexRootWriteStep::Working;
        return true;
    }
    IndexRootWriteStep step()
    {
        if (result_ != IndexRootWriteStep::Working) return result_;
        if (phase_ == 0)
        {
            ::geocaching::storage::VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return finish(IndexRootWriteStep::IoError);
            if (current != volume_) return finish(IndexRootWriteStep::VolumeChanged);
            ++phase_;
            return result_;
        }
        if (phase_ == 1)
        {
            if (storage::sd_exists("/trailmate/geocaching/.state/index")) return finish(IndexRootWriteStep::Invalid);
            ++phase_;
            return result_;
        }
        if (phase_ == 2)
        {
            if (storage::sd_is_directory("/trailmate/geocaching/.state/index")) return finish(IndexRootWriteStep::Invalid);
            ++phase_;
            return result_;
        }
        if (phase_ == 3)
        {
            uint8_t header[24];
            const auto probe = storage::sd_read_file("/trailmate/geocaching/.state/checkpoint/a.gcs", header, sizeof(header));
            if (probe.status != storage::SdFileReadStatus::Missing)
                return finish(probe.status == storage::SdFileReadStatus::Ready || probe.status == storage::SdFileReadStatus::Invalid
                                  ? IndexRootWriteStep::Invalid
                                  : IndexRootWriteStep::IoError);
            ++phase_;
            return result_;
        }
        if (phase_ == 4)
        {
            uint8_t header[24];
            const auto probe = storage::sd_read_file("/trailmate/geocaching/.state/checkpoint/b.gcs", header, sizeof(header));
            if (probe.status != storage::SdFileReadStatus::Missing)
                return finish(probe.status == storage::SdFileReadStatus::Ready || probe.status == storage::SdFileReadStatus::Invalid
                                  ? IndexRootWriteStep::Invalid
                                  : IndexRootWriteStep::IoError);
            ++phase_;
            return result_;
        }
        if (phase_ == 5)
        {
            if (!storage::sd_mkdir("/trailmate/geocaching/.state/index")) return finish(IndexRootWriteStep::IoError);
            if (!writer_.emplace(volume_).begin(0, *bytes_)) return finish(IndexRootWriteStep::Invalid);
            ++phase_;
            return result_;
        }
        const auto status = writer_->step();
        if (status == IndexRootWriteStep::Working) return result_;
        if (status != IndexRootWriteStep::Verified) return finish(status);
        if (phase_ == 6)
        {
            if (!writer_.emplace(volume_).begin(1, *bytes_)) return finish(IndexRootWriteStep::Invalid);
            ++phase_;
            return result_;
        }
        return finish(IndexRootWriteStep::Verified);
    }

  private:
    IndexRootWriteStep finish(IndexRootWriteStep status)
    {
        writer_.reset();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootBytes* bytes_ = nullptr;
    std::optional<SdIndexRootWriter> writer_;
    uint8_t phase_ = 0;
    IndexRootWriteStep result_ = IndexRootWriteStep::Idle;
};
static_assert(sizeof(SdIndexInitialize) <= 192, "Index initialization borrows root storage");
} // namespace platform::esp::arduino_common::geocaching
