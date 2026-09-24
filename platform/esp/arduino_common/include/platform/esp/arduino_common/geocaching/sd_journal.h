#pragma once
#include "geocaching/storage/record_frame.h"
#include "geocaching/storage/transaction.h"
#include "platform/esp/arduino_common/geocaching/sd_volume.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <algorithm>
#include <cstdio>

namespace platform::esp::arduino_common::geocaching
{
enum class JournalWriteResult : uint8_t
{
    Verified,
    Unavailable,
    Exists,
    Invalid,
    IoError,
    UnsupportedVolume,
    CorruptVolume,
    VolumeChanged,
    StateRejected,
    InProgress,
    Busy,
    Cancelled
};

enum class JournalReadResult : uint8_t
{
    Parsed,
    Missing,
    Unavailable,
    Corrupt,
    IoError
};

// Caller owns payload storage and mutation scratch, both retained until the
// transaction is applied. Parsed validates framing/chain only; the store must
// still validate all table values and references atomically before applying.
inline JournalReadResult readJournalTransaction(uint64_t sequence, uint64_t previous_sequence,
                                                uint8_t* buffer, size_t capacity,
                                                ::geocaching::storage::MutationView* mutations,
                                                size_t mutation_capacity,
                                                ::geocaching::storage::TransactionView& out)
{
    out = {};
    if (!buffer || capacity < 24 || !mutations || previous_sequence == UINT64_MAX ||
        sequence != previous_sequence + 1) return JournalReadResult::Corrupt;
    char path[80]{};
    std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/journal/%016llx.gcj",
                  static_cast<unsigned long long>(sequence));
    const auto read = storage::sd_read_file(path, buffer, std::min(capacity, size_t(65560)));
    switch (read.status)
    {
    case storage::SdFileReadStatus::Missing:
        return JournalReadResult::Missing;
    case storage::SdFileReadStatus::Busy:
    case storage::SdFileReadStatus::Unavailable:
        return JournalReadResult::Unavailable;
    case storage::SdFileReadStatus::Invalid:
        return JournalReadResult::Corrupt;
    case storage::SdFileReadStatus::IoError:
        return JournalReadResult::IoError;
    case storage::SdFileReadStatus::Ready:
        break;
    }
    ::geocaching::storage::RecordFrameView frame;
    if (read.file_size != read.bytes_read || read.bytes_read > capacity ||
        !::geocaching::storage::decodeRecordFrame({buffer, read.bytes_read}, frame) ||
        frame.kind != ::geocaching::storage::RecordKind::Transaction || frame.sequence != sequence ||
        !::geocaching::storage::decodeTransaction(frame.payload, previous_sequence, mutations, mutation_capacity, out))
        return JournalReadResult::Corrupt;
    return JournalReadResult::Parsed;
}

// One serialized owner drives begin/step. Input bytes and mutation descriptors
// remain immutable until a terminal result. No method loops over file contents.
// Each data step transfers at most 512 bytes; metadata calls have separate steps.
class SdGeocachingJournal
{
  public:
    explicit SdGeocachingJournal(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    SdGeocachingJournal(const SdGeocachingJournal&) = delete;
    SdGeocachingJournal& operator=(const SdGeocachingJournal&) = delete;

    JournalWriteResult begin(uint64_t previous, ::geocaching::ByteView payload)
    {
        if (result_ == JournalWriteResult::InProgress) return JournalWriteResult::Busy;
        capture_ = nullptr;
        if (!::geocaching::storage::validateTransaction(payload, previous)) return JournalWriteResult::Invalid;
        raw_ = payload;
        streamed_ = false;
        return beginPrepared(previous, payload.size);
    }

    JournalWriteResult begin(uint64_t previous, const ::geocaching::storage::MutationView* mutations, size_t count)
    {
        if (result_ == JournalWriteResult::InProgress) return JournalWriteResult::Busy;
        capture_ = nullptr;
        if (!encoding_.open(previous, mutations, count)) return JournalWriteResult::Invalid;
        raw_ = {};
        streamed_ = true;
        return beginPrepared(previous, encoding_.size());
    }

    JournalWriteResult result() const { return result_; }
    bool mayHaveWritten() const { return may_have_written_; }
    // Optional caller lease populated during the existing readback pass. Its
    // contents are usable only after Verified, never after a partial/failing run.
    bool captureReadback(uint8_t* bytes, size_t capacity)
    {
        if (result_ != JournalWriteResult::InProgress || phase_ != Phase::Checksum || offset_ || !bytes ||
            capacity < header_.size() + payload_size_) return false;
        if (streamed_)
        {
            if (encoding_.inputOverlaps({bytes, header_.size() + payload_size_})) return false;
        }
        else
        {
            const auto a = reinterpret_cast<uintptr_t>(raw_.data), b = reinterpret_cast<uintptr_t>(bytes);
            if (a <= b ? b - a < raw_.size : a - b < header_.size() + payload_size_) return false;
        }
        capture_ = bytes;
        return true;
    }

    // Cancellation is a single boundary action; closing may itself block in the
    // filesystem. Recovery reconciles a published journal, while an unpublished
    // staging file can be replaced by the next serialized writer.
    JournalWriteResult cancel()
    {
        if (result_ != JournalWriteResult::InProgress) return result_;
        if (opened_)
        {
            file_.close();
            opened_ = false;
        }
        return result_ = JournalWriteResult::Cancelled;
    }

    JournalWriteResult step()
    {
        using namespace ::geocaching::storage;
        if (result_ != JournalWriteResult::InProgress) return result_;
        if (phase_ == Phase::CloseFailure)
        {
            file_.close();
            opened_ = false;
            return result_ = failure_;
        }
        if (phase_ != Phase::Checksum &&
            (!storage::sd_card_ready() || storage::sd_external_block_owner_active()))
            return fail(JournalWriteResult::Unavailable);
        const size_t length = std::min(buffer_.size(), payload_size_ - offset_);
        switch (phase_)
        {
        case Phase::Checksum:
            if (!fill(offset_, length)) return fail(JournalWriteResult::Invalid);
            crc_ = sys::crc32(buffer_.data(), length, crc_);
            offset_ += length;
            if (offset_ == payload_size_)
            {
                finishRecordHeader(header_, crc_);
                offset_ = 0;
                phase_ = Phase::Directory;
            }
            break;
        case Phase::Directory:
            if (!storage::sd_is_directory("/trailmate/geocaching/.state/journal"))
                return fail(JournalWriteResult::Unavailable);
            phase_ = Phase::VolumeBefore;
            break;
        case Phase::VolumeBefore:
        case Phase::VolumeAfter:
        case Phase::PublishedVolume:
        {
            VolumeInstance volume;
            const auto inspected = inspectSdVolume(volume);
            if (inspected != SdVolumeResult::Ready) return fail(volumeFailure(inspected));
            if (volume != volume_) return fail(JournalWriteResult::VolumeChanged);
            if (phase_ == Phase::PublishedVolume) return result_ = JournalWriteResult::Verified;
            if (phase_ == Phase::VolumeAfter)
            {
                phase_ = Phase::Publish;
                break;
            }
            phase_ = Phase::Exists;
            break;
        }
        case Phase::Exists:
            if (storage::sd_exists(path_.data())) return fail(JournalWriteResult::Exists);
            phase_ = Phase::OpenWrite;
            break;
        case Phase::OpenWrite:
            may_have_written_ = true;
            if (!file_.open(kStagingPath, "w")) return fail(JournalWriteResult::IoError);
            opened_ = true;
            phase_ = Phase::WriteHeader;
            break;
        case Phase::WriteHeader:
            if (file_.write(header_.data(), header_.size()) != header_.size()) return fail(JournalWriteResult::IoError);
            phase_ = Phase::WritePayload;
            break;
        case Phase::WritePayload:
            if (!fill(offset_, length)) return fail(JournalWriteResult::Invalid);
            if (file_.write(buffer_.data(), length) != length) return fail(JournalWriteResult::IoError);
            offset_ += length;
            if (offset_ == payload_size_) phase_ = Phase::Flush;
            break;
        case Phase::Flush:
            if (!file_.flush()) return fail(JournalWriteResult::IoError);
            phase_ = Phase::CloseWrite;
            break;
        case Phase::CloseWrite:
            file_.close();
            opened_ = false;
            phase_ = Phase::OpenRead;
            break;
        case Phase::OpenRead:
            if (!file_.open(kStagingPath, "r")) return fail(JournalWriteResult::IoError);
            opened_ = true;
            phase_ = Phase::ReadSize;
            break;
        case Phase::ReadSize:
            if (file_.size() != header_.size() + payload_size_) return fail(JournalWriteResult::IoError);
            phase_ = Phase::ReadHeader;
            break;
        case Phase::ReadHeader:
            if (file_.read(buffer_.data(), header_.size()) != static_cast<int>(header_.size()) ||
                std::memcmp(buffer_.data(), header_.data(), header_.size())) return fail(JournalWriteResult::IoError);
            if (capture_) std::memcpy(capture_, buffer_.data(), header_.size());
            offset_ = 0;
            phase_ = Phase::ReadPayload;
            break;
        case Phase::ReadPayload:
            if (file_.read(buffer_.data(), length) != static_cast<int>(length) ||
                !matches(offset_, length)) return fail(JournalWriteResult::IoError);
            if (capture_) std::memcpy(capture_ + header_.size() + offset_, buffer_.data(), length);
            offset_ += length;
            if (offset_ == payload_size_) phase_ = Phase::CloseRead;
            break;
        case Phase::CloseRead:
            file_.close();
            opened_ = false;
            phase_ = Phase::VolumeAfter;
            break;
        case Phase::Publish:
            if (!storage::sd_rename(kStagingPath, path_.data())) return fail(JournalWriteResult::IoError);
            phase_ = Phase::PublishedVolume;
            break;
        case Phase::CloseFailure:
            break;
        }
        return result_;
    }

  private:
    // A single storage owner serializes all transactions. Keep incomplete bytes
    // outside the journal inventory; only a flushed, verified frame is published.
    static constexpr const char* kStagingPath = "/trailmate/geocaching/.state/journal.pending";
    enum class Phase : uint8_t
    {
        Checksum,
        Directory,
        VolumeBefore,
        Exists,
        OpenWrite,
        WriteHeader,
        WritePayload,
        Flush,
        CloseWrite,
        OpenRead,
        ReadSize,
        ReadHeader,
        ReadPayload,
        CloseRead,
        VolumeAfter,
        Publish,
        PublishedVolume,
        CloseFailure
    };

    JournalWriteResult beginPrepared(uint64_t previous, size_t size)
    {
        if (previous == UINT64_MAX ||
            !::geocaching::storage::makeRecordPrefix(::geocaching::storage::RecordKind::Transaction,
                                                     previous + 1, size, header_))
            return JournalWriteResult::Invalid;
        payload_size_ = size;
        offset_ = 0;
        may_have_written_ = false;
        crc_ = sys::crc32(header_.data(), 20);
        std::snprintf(path_.data(), path_.size(), "/trailmate/geocaching/.state/journal/%016llx.gcj",
                      static_cast<unsigned long long>(previous + 1));
        phase_ = Phase::Checksum;
        return result_ = JournalWriteResult::InProgress;
    }

    bool fill(size_t offset, size_t length)
    {
        if (!streamed_)
        {
            std::memcpy(buffer_.data(), raw_.data + offset, length);
            return true;
        }
        size_t written = 0;
        return encoding_.readSlice(offset, buffer_.data(), length, written) && written == length;
    }
    bool matches(size_t offset, size_t length) const
    {
        return streamed_ ? encoding_.matchesSlice(offset, {buffer_.data(), length})
                         : !std::memcmp(buffer_.data(), raw_.data + offset, length);
    }
    JournalWriteResult fail(JournalWriteResult result)
    {
        if (opened_)
        {
            failure_ = result;
            phase_ = Phase::CloseFailure;
            return result_;
        }
        return result_ = result;
    }
    static JournalWriteResult volumeFailure(SdVolumeResult result)
    {
        switch (result)
        {
        case SdVolumeResult::Missing:
        case SdVolumeResult::Unavailable:
            return JournalWriteResult::Unavailable;
        case SdVolumeResult::Unsupported:
            return JournalWriteResult::UnsupportedVolume;
        case SdVolumeResult::Corrupt:
            return JournalWriteResult::CorruptVolume;
        default:
            return JournalWriteResult::IoError;
        }
    }

    const ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::TransactionEncoding encoding_;
    ::geocaching::ByteView raw_;
    uint8_t* capture_ = nullptr;
    storage::SdRuntimeFile file_;
    ::geocaching::storage::RecordHeader header_{};
    std::array<uint8_t, 512> buffer_{};
    std::array<char, 80> path_{};
    size_t offset_ = 0, payload_size_ = 0;
    uint32_t crc_ = 0;
    Phase phase_ = Phase::Checksum;
    JournalWriteResult result_ = JournalWriteResult::Invalid, failure_ = JournalWriteResult::Invalid;
    bool streamed_ = false, opened_ = false, may_have_written_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
