#pragma once
#include "geocaching/storage/checkpoint_encoding.h"
#include "geocaching/storage/record_frame.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"
#include <algorithm>

namespace platform::esp::arduino_common::geocaching
{
enum class CheckpointWriteStep : uint8_t
{
    Idle,
    Ready,
    Working,
    Verified,
    Busy,
    Unavailable,
    Exists,
    Invalid,
    IoError,
    VolumeChanged
};

// Serialized storage-worker owner only. A fresh digest and pinned snapshot are
// required for each writer. Ready releases the preceding page's borrowed input.
// Verified describes a staging file, never permission to overwrite a referenced
// checkpoint slot or delete a journal. The publication coordinator owns that.
template <class Digest>
class SdCheckpointWriter
{
  public:
    static constexpr const char* path = "/trailmate/geocaching/.state/staging/checkpoint.gcs";
    SdCheckpointWriter(const ::geocaching::storage::VolumeInstance& volume, Digest& digest)
        : volume_(volume), digest_(digest) {}
    bool begin(uint64_t sequence)
    {
        if (result_ != CheckpointWriteStep::Idle || !sequence) return false;
        sequence_ = sequence;
        result_ = CheckpointWriteStep::Working;
        phase_ = Phase::InitialVolume;
        return true;
    }
    bool page(const ::geocaching::storage::MutationView* rows, size_t count)
    {
        using namespace ::geocaching::storage;
        MutationView previous{last_table_, {last_key_.data(), last_key_size_}, {}, false};
        if (result_ != CheckpointWriteStep::Ready || pages_ == UINT64_MAX || count > UINT64_MAX - entries_ ||
            !encoding_.open(pages_, rows, count, last_key_size_ ? &previous : nullptr)) return false;
        if (count)
        {
            const auto& last = rows[count - 1];
            last_table_ = last.table;
            last_key_size_ = last.key.size;
            std::memcpy(last_key_.data(), last.key.data, last.key.size);
        }
        pending_count_ = count;
        return prepare(RecordKind::CheckpointPage, encoding_.size());
    }
    bool finish()
    {
        if (result_ != CheckpointWriteStep::Ready) return false;
        uint8_t hash[32];
        if (!digest_.finalize(hash, sizeof(hash)) ||
            !::geocaching::storage::encodeCheckpointTail(pages_, entries_, {hash, sizeof(hash)},
                                                         tail_.data(), tail_.size(), tail_size_))
        {
            result_ = CheckpointWriteStep::Invalid;
            return false;
        }
        ending_ = true;
        return prepare(::geocaching::storage::RecordKind::CheckpointTail, tail_size_);
    }
    CheckpointWriteStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != CheckpointWriteStep::Working) return result_;
        // Busy is transient. Do not close a handle through another SD owner.
        if (storage::sd_external_block_owner_active()) return CheckpointWriteStep::Busy;
        if (!storage::sd_card_ready()) return CheckpointWriteStep::Unavailable;
        if (phase_ == Phase::InitialVolume || phase_ == Phase::VolumeBefore || phase_ == Phase::VolumeAfter)
        {
            VolumeInstance current;
            const auto checked = inspectSdVolume(current);
            if (checked == SdVolumeResult::Unavailable) return CheckpointWriteStep::Unavailable;
            if (checked != SdVolumeResult::Ready) return fail(checked == SdVolumeResult::IoError ? CheckpointWriteStep::IoError : CheckpointWriteStep::Invalid);
            if (current != volume_) return fail(CheckpointWriteStep::VolumeChanged);
            if (phase_ == Phase::InitialVolume) phase_ = Phase::Exists;
            else if (phase_ == Phase::VolumeBefore) phase_ = Phase::OpenWrite;
            else
            {
                length_ += header_.size() + payload_size_;
                if (!ending_)
                {
                    ++pages_;
                    entries_ += pending_count_;
                }
                encoding_ = {};
                return result_ = ending_ ? CheckpointWriteStep::Verified : CheckpointWriteStep::Ready;
            }
            return result_;
        }
        switch (phase_)
        {
        case Phase::Exists:
            if (storage::sd_exists(path)) return fail(CheckpointWriteStep::Exists);
            return result_ = CheckpointWriteStep::Ready;
        case Phase::Checksum:
        {
            size_t count = 0;
            if (!fill(offset_, expected_.data(), expected_.size(), count) || !count) return fail(CheckpointWriteStep::Invalid);
            crc_ = sys::crc32(expected_.data(), count, crc_);
            offset_ += count;
            if (offset_ == payload_size_)
            {
                finishRecordHeader(header_, crc_);
                offset_ = 0;
                phase_ = Phase::VolumeBefore;
            }
            break;
        }
        case Phase::OpenWrite:
            if (!file_.open(path, "a")) return fail(CheckpointWriteStep::IoError);
            phase_ = Phase::WriteSize;
            break;
        case Phase::WriteSize:
            if (file_.size() != length_) return fail(CheckpointWriteStep::Invalid);
            phase_ = Phase::WriteHeader;
            break;
        case Phase::WriteHeader:
            if (file_.write(header_.data(), header_.size()) != header_.size()) return fail(CheckpointWriteStep::IoError);
            phase_ = Phase::WritePayload;
            break;
        case Phase::WritePayload:
        {
            size_t count = 0;
            if (!fill(offset_, expected_.data(), expected_.size(), count) || !count) return fail(CheckpointWriteStep::Invalid);
            if (file_.write(expected_.data(), count) != count) return fail(CheckpointWriteStep::IoError);
            offset_ += count;
            if (offset_ == payload_size_) phase_ = Phase::Flush;
            break;
        }
        case Phase::Flush:
            if (!file_.flush()) return fail(CheckpointWriteStep::IoError);
            phase_ = Phase::CloseWrite;
            break;
        case Phase::CloseWrite:
            file_.close();
            phase_ = Phase::OpenRead;
            break;
        case Phase::OpenRead:
            if (!file_.open(path, "r")) return fail(CheckpointWriteStep::IoError);
            phase_ = Phase::ReadSize;
            break;
        case Phase::ReadSize:
            if (file_.size() != length_ + header_.size() + payload_size_) return fail(CheckpointWriteStep::Invalid);
            phase_ = Phase::Seek;
            break;
        case Phase::Seek:
            if (!file_.seek(length_)) return fail(CheckpointWriteStep::IoError);
            offset_ = 0;
            phase_ = Phase::ReadHeader;
            break;
        case Phase::ReadHeader:
        {
            const int count = file_.read(readback_.data() + offset_, header_.size() - offset_);
            if (count <= 0 || size_t(count) > header_.size() - offset_) return fail(CheckpointWriteStep::IoError);
            offset_ += count;
            if (offset_ != header_.size()) break;
            if (std::memcmp(readback_.data(), header_.data(), header_.size())) return fail(CheckpointWriteStep::Invalid);
            if (!ending_) digest_.update(readback_.data(), header_.size());
            crc_ = sys::crc32(header_.data(), 20);
            offset_ = 0;
            phase_ = Phase::ReadPayload;
            break;
        }
        case Phase::ReadPayload:
        {
            size_t wanted = 0;
            if (!fill(offset_, expected_.data(), expected_.size(), wanted) || !wanted) return fail(CheckpointWriteStep::Invalid);
            const int count = file_.read(readback_.data(), wanted);
            if (count <= 0 || size_t(count) > wanted) return fail(CheckpointWriteStep::IoError);
            if (std::memcmp(readback_.data(), expected_.data(), count)) return fail(CheckpointWriteStep::Invalid);
            crc_ = sys::crc32(readback_.data(), count, crc_);
            if (!ending_) digest_.update(readback_.data(), count);
            offset_ += count;
            if (offset_ == payload_size_)
            {
                RecordHeader verified = header_;
                finishRecordHeader(verified, crc_);
                if (verified != header_) return fail(CheckpointWriteStep::Invalid);
                phase_ = Phase::CloseRead;
            }
            break;
        }
        case Phase::CloseRead:
            file_.close();
            phase_ = Phase::VolumeAfter;
            break;
        default:
            return fail(CheckpointWriteStep::Invalid);
        }
        return result_;
    }

  private:
    bool prepare(::geocaching::storage::RecordKind kind, size_t size)
    {
        if (length_ > UINT32_MAX - 24 || size > UINT32_MAX - 24 - length_ ||
            !::geocaching::storage::makeRecordPrefix(kind, sequence_, size, header_))
        {
            result_ = CheckpointWriteStep::Invalid;
            return false;
        }
        payload_size_ = size;
        offset_ = 0;
        crc_ = sys::crc32(header_.data(), 20);
        phase_ = Phase::Checksum;
        result_ = CheckpointWriteStep::Working;
        return true;
    }
    bool fill(size_t offset, uint8_t* output, size_t capacity, size_t& count) const
    {
        if (!ending_) return encoding_.readSlice(offset, output, capacity, count);
        count = std::min(capacity, tail_size_ - offset);
        std::memcpy(output, tail_.data() + offset, count);
        return true;
    }
    CheckpointWriteStep fail(CheckpointWriteStep result)
    {
        file_.close();
        encoding_ = {};
        return result_ = result;
    }
    enum class Phase : uint8_t
    {
        InitialVolume,
        Exists,
        Checksum,
        VolumeBefore,
        OpenWrite,
        WriteSize,
        WriteHeader,
        WritePayload,
        Flush,
        CloseWrite,
        OpenRead,
        ReadSize,
        Seek,
        ReadHeader,
        ReadPayload,
        CloseRead,
        VolumeAfter
    };
    ::geocaching::storage::VolumeInstance volume_;
    Digest& digest_;
    storage::SdRuntimeFile file_;
    ::geocaching::storage::CheckpointPageEncoding encoding_;
    ::geocaching::storage::RecordHeader header_{};
    std::array<uint8_t, 256> expected_{}, readback_{};
    std::array<uint8_t, 96> last_key_{};
    std::array<uint8_t, 64> tail_{};
    uint64_t sequence_ = 0, pages_ = 0, entries_ = 0, length_ = 0;
    size_t pending_count_ = 0, last_key_size_ = 0, payload_size_ = 0, offset_ = 0, tail_size_ = 0;
    uint32_t crc_ = 0;
    uint8_t last_table_ = 0;
    bool ending_ = false;
    Phase phase_ = Phase::InitialVolume;
    CheckpointWriteStep result_ = CheckpointWriteStep::Idle;
};
} // namespace platform::esp::arduino_common::geocaching
