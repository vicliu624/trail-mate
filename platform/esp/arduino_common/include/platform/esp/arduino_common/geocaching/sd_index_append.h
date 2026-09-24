#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_path.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"

namespace platform::esp::arduino_common::geocaching
{
enum class IndexAppendStep : uint8_t
{
    Idle,
    Working,
    Verified,
    Invalid,
    IoError,
    VolumeChanged
};

// Owner prepares the slot/table directories and supplies a validated journal
// locator. Verified applies to ONE entry only: never publish the transaction
// watermark until every entry has reached Verified. Partial tails require rebuild.
class SdIndexAppend
{
  public:
    SdIndexAppend(const ::geocaching::storage::VolumeInstance& volume, char slot) : volume_(volume), slot_(slot) {}
    uint64_t writtenLength() const { return result_ == IndexAppendStep::Verified ? length_ + (duplicate_ ? 0 : ::geocaching::storage::kIndexEntrySize) : 0; }
    bool begin(const ::geocaching::storage::IndexedMutation& entry)
    {
        if (result_ != IndexAppendStep::Idle || (slot_ != 'a' && slot_ != 'b') || !::geocaching::storage::encodeIndexEntry(volume_, entry, bytes_)) return false;
        result_ = IndexAppendStep::Working;
        return true;
    }
    IndexAppendStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexAppendStep::Working) return result_;
        if (phase_ == Phase::Volume || phase_ == Phase::VerifyVolume)
        {
            VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexAppendStep::IoError);
            if (current != volume_) return fail(IndexAppendStep::VolumeChanged);
            if (phase_ == Phase::VerifyVolume) return result_ = IndexAppendStep::Verified;
            phase_ = Phase::OpenWrite;
            return result_;
        }
        if (phase_ == Phase::OpenWrite || phase_ == Phase::OpenRead)
        {
            char path[80];
            if (!indexShardPath(slot_, bytes_[20], {bytes_.data() + 52, bytes_[21]}, path, sizeof(path)) ||
                !file_.open(path, phase_ == Phase::OpenWrite ? "a+" : "r")) return fail(IndexAppendStep::IoError);
            phase_ = phase_ == Phase::OpenWrite ? Phase::Size : Phase::SeekVerify;
            return result_;
        }
        if (phase_ == Phase::Size)
        {
            length_ = file_.size();
            if (length_ % kIndexEntrySize || length_ > UINT64_MAX - kIndexEntrySize) return fail(IndexAppendStep::Invalid);
            verify_offset_ = length_;
            tail_offset_ = length_ ? length_ - kIndexEntrySize : 0;
            phase_ = length_ ? Phase::SeekTail : Phase::Write;
            return result_;
        }
        if (phase_ == Phase::SeekTail || phase_ == Phase::SeekVerify)
        {
            const bool tail = phase_ == Phase::SeekTail;
            if (!file_.seek(tail ? tail_offset_ : verify_offset_)) return fail(IndexAppendStep::IoError);
            read_ = 0;
            phase_ = tail ? Phase::ReadTail : Phase::ReadVerify;
            return result_;
        }
        if (phase_ == Phase::ReadTail || phase_ == Phase::ReadVerify)
        {
            const int count = file_.read(verify_.data() + read_, verify_.size() - read_);
            if (count < 0) return fail(IndexAppendStep::IoError);
            if (!count || static_cast<size_t>(count) > verify_.size() - read_) return fail(IndexAppendStep::Invalid);
            read_ += static_cast<uint16_t>(count);
            if (read_ != verify_.size()) return result_;
            if (phase_ == Phase::ReadVerify)
            {
                if (verify_ != bytes_) return fail(IndexAppendStep::Invalid);
                phase_ = Phase::CheckSize;
                return result_;
            }
            IndexedMutation previous, next;
            if (!decodeIndexEntry({verify_.data(), verify_.size()}, volume_, previous) ||
                !decodeIndexEntry({bytes_.data(), bytes_.size()}, volume_, next) || previous.table != next.table ||
                (::sys::crc32(previous.key.data, previous.key.size) & 0xff) != (::sys::crc32(next.key.data, next.key.size) & 0xff) ||
                previous.location.record_sequence > next.location.record_sequence) return fail(IndexAppendStep::Invalid);
            if (previous.location.record_sequence == next.location.record_sequence && previous.key.size == next.key.size &&
                !std::memcmp(previous.key.data, next.key.data, next.key.size))
            {
                if (verify_ != bytes_) return fail(IndexAppendStep::Invalid);
                duplicate_ = true;
                verify_offset_ = tail_offset_;
                phase_ = Phase::Flush;
            }
            else if (previous.location.record_sequence == next.location.record_sequence && tail_offset_)
            {
                tail_offset_ -= kIndexEntrySize;
                phase_ = Phase::SeekTail;
            }
            else phase_ = Phase::Write;
            return result_;
        }
        if (phase_ == Phase::Write)
        {
            if (file_.write(bytes_.data(), bytes_.size()) != bytes_.size()) return fail(IndexAppendStep::IoError);
            phase_ = Phase::Flush;
            return result_;
        }
        if (phase_ == Phase::Flush)
        {
            if (!file_.flush()) return fail(IndexAppendStep::IoError);
            phase_ = Phase::CloseWrite;
            return result_;
        }
        if (phase_ == Phase::CloseWrite)
        {
            file_.close();
            phase_ = Phase::OpenRead;
            return result_;
        }
        if (phase_ == Phase::CheckSize)
        {
            if (file_.size() != length_ + (duplicate_ ? 0 : kIndexEntrySize)) return fail(IndexAppendStep::Invalid);
            phase_ = Phase::CloseRead;
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
        Size,
        SeekTail,
        ReadTail,
        Write,
        Flush,
        CloseWrite,
        OpenRead,
        SeekVerify,
        ReadVerify,
        CheckSize,
        CloseRead,
        VerifyVolume
    };
    IndexAppendStep fail(IndexAppendStep result)
    {
        file_.close();
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexEntryBytes bytes_{}, verify_{};
    storage::SdRuntimeFile file_;
    uint64_t length_ = 0, verify_offset_ = 0, tail_offset_ = 0;
    uint16_t read_ = 0;
    char slot_;
    bool duplicate_ = false;
    Phase phase_ = Phase::Volume;
    IndexAppendStep result_ = IndexAppendStep::Idle;
};
static_assert(sizeof(SdIndexAppend) <= 384, "Index append retains metadata only");
} // namespace platform::esp::arduino_common::geocaching
