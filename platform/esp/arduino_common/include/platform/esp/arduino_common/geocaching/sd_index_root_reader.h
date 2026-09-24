#pragma once
#include "geocaching/storage/index_root.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"
#include <cstdio>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexRootReadStep : uint8_t
{
    Idle,
    Working,
    Ready,
    Invalid,
    IoError,
    VolumeChanged
};

// Caller leases two disjoint metadata buffers until the selected borrowed view
// is consumed. No full-index or value allocation is performed by this reader.
class SdIndexRootReader
{
  public:
    explicit SdIndexRootReader(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second)
    {
        if (result_ != IndexRootReadStep::Idle || &first == &second) return false;
        buffers_[0] = &first;
        buffers_[1] = &second;
        result_ = IndexRootReadStep::Working;
        return true;
    }
    bool selected(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        if (result_ != IndexRootReadStep::Ready) return false;
        out = selected_;
        return true;
    }
    int selectedCopy() const { return result_ == IndexRootReadStep::Ready ? selected_copy_ : -1; }
    IndexRootReadStep step()
    {
        if (result_ != IndexRootReadStep::Working) return result_;
        if (phase_ == 0 || phase_ == 3)
        {
            ::geocaching::storage::VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return result_ = IndexRootReadStep::IoError;
            if (current != volume_) return result_ = IndexRootReadStep::VolumeChanged;
            if (phase_ == 3) return result_ = IndexRootReadStep::Ready;
            phase_ = 1;
            return result_;
        }
        const unsigned copy = phase_ - 1;
        auto& buffer = *buffers_[copy];
        char path[64];
        std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/index/root.h%u", copy);
        const auto read = storage::sd_read_file(path, buffer.data(), buffer.size());
        if (read.status == storage::SdFileReadStatus::Missing || read.status == storage::SdFileReadStatus::Invalid)
            return result_ = IndexRootReadStep::Invalid;
        if (read.status != storage::SdFileReadStatus::Ready) return result_ = IndexRootReadStep::IoError;
        if (read.file_size != buffer.size() || read.bytes_read != buffer.size() ||
            !::geocaching::storage::decodeIndexRoot({buffer.data(), buffer.size()}, volume_, roots_[copy]))
            return result_ = IndexRootReadStep::Invalid;
        if (copy == 1)
        {
            if (!::geocaching::storage::selectIndexRoot(roots_[0], roots_[1], selected_)) return result_ = IndexRootReadStep::Invalid;
            selected_copy_ = selected_.revision == roots_[0].revision ? 0 : 1;
        }
        ++phase_;
        return result_;
    }

  private:
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootBytes* buffers_[2]{};
    ::geocaching::storage::IndexRootView roots_[2], selected_;
    uint8_t phase_ = 0, selected_copy_ = 0;
    IndexRootReadStep result_ = IndexRootReadStep::Idle;
};
static_assert(sizeof(SdIndexRootReader) <= 192, "Root readers borrow their metadata buffers");
} // namespace platform::esp::arduino_common::geocaching
