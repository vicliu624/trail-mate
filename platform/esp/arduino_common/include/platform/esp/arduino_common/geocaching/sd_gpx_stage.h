#pragma once
#include "geocaching/gpx/write_record.h"
#include "gps/gpx/window_sink.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include <array>
#include <cstdio>

namespace platform::esp::arduino_common::geocaching
{
enum class StageResult : uint8_t
{
    Written,
    Unavailable,
    AlreadyExists,
    InvalidTransaction,
    IoError,
    InvalidRecord,
    InProgress,
    Cancelled
};

// The owner pins the verified response and crypto provider until terminal state.
// Written means staging was flushed and read back against the existing GPX
// serializer, not that the target file was installed. The optional observer
// receives verified readback chunks; its result is usable only after Written.
class SdGpxStage
{
  public:
    using VerifiedBytes = void (*)(void*, const uint8_t*, size_t);
    const char* path() const { return path_.data(); }
    uint64_t bytes() const { return bytes_; }
    StageResult begin(const std::array<uint8_t, 16>& transaction_id,
                      const ::geocaching::protocol::VerifiedRecordView& record,
                      ::geocaching::protocol::RecordCrypto& crypto,
                      VerifiedBytes verified_bytes = nullptr, void* context = nullptr)
    {
        if (started_) return StageResult::InvalidTransaction;
        started_ = true;
        bool nonzero = false;
        char hex[33]{};
        constexpr char alphabet[] = "0123456789abcdef";
        for (size_t i = 0; i < transaction_id.size(); ++i)
        {
            nonzero = nonzero || transaction_id[i] != 0;
            hex[i * 2] = alphabet[transaction_id[i] >> 4];
            hex[i * 2 + 1] = alphabet[transaction_id[i] & 15];
        }
        if (!nonzero) return result_ = StageResult::InvalidTransaction;
        ::gps::gpx::WindowSink count(0, nullptr, 0);
        if (!::geocaching::gpx::writeGeocacheGpx(record, crypto, count) ||
            !count.total() || count.total() > ::geocaching::kMaxGpxBytes) return result_ = StageResult::InvalidRecord;
        record_ = record;
        crypto_ = &crypto;
        bytes_ = count.total();
        verified_bytes_ = verified_bytes;
        context_ = context;
        std::snprintf(path_.data(), path_.size(), "/trailmate/geocaching/.state/staging/%s.gpx", hex);
        return result_ = StageResult::InProgress;
    }
    StageResult step()
    {
        if (result_ != StageResult::InProgress) return result_;
        if (phase_ == Phase::CloseFailure)
        {
            file_.close();
            opened_ = false;
            return result_ = failure_;
        }
        if (!storage::sd_card_ready() || storage::sd_external_block_owner_active()) return fail(StageResult::Unavailable);
        switch (phase_)
        {
        case Phase::Directory:
            if (!storage::sd_is_directory("/trailmate/geocaching/.state/staging")) return fail(StageResult::Unavailable);
            phase_ = Phase::Exists;
            break;
        case Phase::Exists:
            if (storage::sd_exists(path_.data())) return fail(StageResult::AlreadyExists);
            phase_ = Phase::Open;
            break;
        case Phase::Open:
            if (!file_.open(path_.data(), "w")) return fail(StageResult::IoError);
            opened_ = true;
            phase_ = Phase::Write;
            break;
        case Phase::Write:
        {
            const size_t remaining = static_cast<size_t>(bytes_ - offset_);
            const size_t count = remaining < buffer_.size() ? remaining : buffer_.size();
            ::gps::gpx::WindowSink window(offset_, buffer_.data(), count);
            if (!::geocaching::gpx::writeGeocacheGpx(record_, *crypto_, window) ||
                window.total() != bytes_ || window.written() != count) return fail(StageResult::InvalidRecord);
            if (file_.write(buffer_.data(), count) != count) return fail(StageResult::IoError);
            offset_ += count;
            if (offset_ == bytes_) phase_ = Phase::Flush;
            break;
        }
        case Phase::Flush:
            if (!file_.flush()) return fail(StageResult::IoError);
            phase_ = Phase::Size;
            break;
        case Phase::Size:
            if (file_.size() != bytes_) return fail(StageResult::IoError);
            phase_ = Phase::Close;
            break;
        case Phase::Close:
            file_.close();
            opened_ = false;
            phase_ = Phase::OpenRead;
            break;
        case Phase::OpenRead:
            if (!file_.open(path_.data(), "r")) return fail(StageResult::IoError);
            opened_ = true;
            offset_ = 0;
            phase_ = Phase::ReadSize;
            break;
        case Phase::ReadSize:
            if (file_.size() != bytes_) return fail(StageResult::IoError);
            phase_ = Phase::Read;
            break;
        case Phase::Read:
        {
            const size_t remaining = static_cast<size_t>(bytes_ - offset_);
            const size_t count = remaining < buffer_.size() ? remaining : buffer_.size();
            if (file_.read(buffer_.data(), count) != static_cast<int>(count)) return fail(StageResult::IoError);
            ::gps::gpx::WindowSink compare(offset_, {reinterpret_cast<const char*>(buffer_.data()), count});
            if (!::geocaching::gpx::writeGeocacheGpx(record_, *crypto_, compare) ||
                compare.total() != bytes_ || compare.written() != count) return fail(StageResult::IoError);
            if (verified_bytes_) verified_bytes_(context_, buffer_.data(), count);
            offset_ += count;
            if (offset_ == bytes_) phase_ = Phase::FinalSize;
            break;
        }
        case Phase::FinalSize:
            if (file_.size() != bytes_) return fail(StageResult::IoError);
            phase_ = Phase::CloseRead;
            break;
        case Phase::CloseRead:
            file_.close();
            opened_ = false;
            return result_ = StageResult::Written;
        case Phase::CloseFailure:
            break;
        }
        return result_;
    }
    StageResult cancel()
    {
        if (result_ != StageResult::InProgress) return result_;
        if (opened_)
        {
            file_.close();
            opened_ = false;
        }
        return result_ = StageResult::Cancelled;
    }

  private:
    StageResult fail(StageResult result)
    {
        if (opened_)
        {
            failure_ = result;
            phase_ = Phase::CloseFailure;
            return result_;
        }
        return result_ = result;
    }
    enum class Phase : uint8_t
    {
        Directory,
        Exists,
        Open,
        Write,
        Flush,
        Size,
        Close,
        OpenRead,
        ReadSize,
        Read,
        FinalSize,
        CloseRead,
        CloseFailure
    };
    storage::SdRuntimeFile file_;
    ::geocaching::protocol::VerifiedRecordView record_;
    ::geocaching::protocol::RecordCrypto* crypto_ = nullptr;
    VerifiedBytes verified_bytes_ = nullptr;
    void* context_ = nullptr;
    std::array<uint8_t, 512> buffer_{};
    std::array<char, 96> path_{};
    uint64_t bytes_ = 0;
    size_t offset_ = 0;
    Phase phase_ = Phase::Directory;
    StageResult result_ = StageResult::InvalidTransaction, failure_ = StageResult::IoError;
    bool started_ = false, opened_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
