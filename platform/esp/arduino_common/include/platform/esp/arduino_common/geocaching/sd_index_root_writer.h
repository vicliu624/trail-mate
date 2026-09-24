#pragma once
#include "geocaching/storage/index_root.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"
#include <algorithm>
#include <cstdio>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexRootWriteStep : uint8_t
{
    Idle,
    Working,
    Verified,
    Invalid,
    IoError,
    VolumeChanged
};

// Final publication primitive. Owner validates the root transition, completes
// all shard writes, and chooses the non-current copy before calling begin().
// Encoded metadata is borrowed and immutable through the terminal result.
class SdIndexRootWriter
{
  public:
    explicit SdIndexRootWriter(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(unsigned copy, const ::geocaching::storage::IndexRootBytes& bytes)
    {
        ::geocaching::storage::IndexRootView checked;
        if (result_ != IndexRootWriteStep::Idle || copy > 1 ||
            !::geocaching::storage::decodeIndexRoot({bytes.data(), bytes.size()}, volume_, checked)) return false;
        copy_ = static_cast<uint8_t>(copy);
        bytes_ = &bytes;
        result_ = IndexRootWriteStep::Working;
        return true;
    }
    IndexRootWriteStep step()
    {
        if (result_ != IndexRootWriteStep::Working) return result_;
        if (phase_ == Phase::Volume || phase_ == Phase::VerifyVolume)
        {
            ::geocaching::storage::VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexRootWriteStep::IoError);
            if (current != volume_) return fail(IndexRootWriteStep::VolumeChanged);
            if (phase_ == Phase::VerifyVolume) return result_ = IndexRootWriteStep::Verified;
            phase_ = Phase::OpenWrite;
            return result_;
        }
        if (phase_ == Phase::OpenWrite || phase_ == Phase::OpenRead)
        {
            char path[64];
            std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/index/root.h%u", static_cast<unsigned>(copy_));
            if (!file_.open(path, phase_ == Phase::OpenWrite ? "w" : "r")) return fail(IndexRootWriteStep::IoError);
            phase_ = phase_ == Phase::OpenWrite ? Phase::Write : Phase::Size;
            return result_;
        }
        if (phase_ == Phase::Write)
        {
            if (file_.write(bytes_->data(), bytes_->size()) != bytes_->size()) return fail(IndexRootWriteStep::IoError);
            phase_ = Phase::Flush;
            return result_;
        }
        if (phase_ == Phase::Flush)
        {
            if (!file_.flush()) return fail(IndexRootWriteStep::IoError);
            phase_ = Phase::CloseWrite;
            return result_;
        }
        if (phase_ == Phase::CloseWrite)
        {
            file_.close();
            phase_ = Phase::OpenRead;
            return result_;
        }
        if (phase_ == Phase::Size || phase_ == Phase::FinalSize)
        {
            if (file_.size() != bytes_->size()) return fail(IndexRootWriteStep::Invalid);
            phase_ = phase_ == Phase::Size ? Phase::Readback : Phase::CloseRead;
            return result_;
        }
        if (phase_ == Phase::Readback)
        {
            const auto count = std::min(verify_.size(), bytes_->size() - verified_);
            const int read = file_.read(verify_.data(), count);
            if (read < 0) return fail(IndexRootWriteStep::IoError);
            if (!read || static_cast<size_t>(read) > count || std::memcmp(verify_.data(), bytes_->data() + verified_, read)) return fail(IndexRootWriteStep::Invalid);
            verified_ += static_cast<size_t>(read);
            if (verified_ == bytes_->size()) phase_ = Phase::FinalSize;
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
        OpenWrite,
        Write,
        Flush,
        CloseWrite,
        OpenRead,
        Size,
        Readback,
        FinalSize,
        CloseRead,
        VerifyVolume
    };
    IndexRootWriteStep fail(IndexRootWriteStep result)
    {
        file_.close();
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    const ::geocaching::storage::IndexRootBytes* bytes_ = nullptr;
    std::array<uint8_t, 64> verify_{};
    storage::SdRuntimeFile file_;
    size_t verified_ = 0;
    uint8_t copy_ = 0;
    Phase phase_ = Phase::Volume;
    IndexRootWriteStep result_ = IndexRootWriteStep::Idle;
};
static_assert(sizeof(SdIndexRootWriter) <= 128, "Root publishing borrows metadata and verifies small slices");
} // namespace platform::esp::arduino_common::geocaching
