#pragma once
#include "platform/esp/arduino_common/geocaching/sd_index_append.h"
#include "platform/esp/arduino_common/geocaching/sd_index_head_reader.h"
#include "platform/esp/arduino_common/geocaching/sd_index_head_writer.h"
#include "platform/esp/arduino_common/geocaching/sd_index_root_writer.h"
#include <variant>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexTransactionStep : uint8_t
{
    Idle,
    Working,
    Verified,
    Invalid,
    IoError,
    VolumeChanged
};

// Storage owner prevalidates table values/references and selects a valid
// generation. Parent bitmap and journal frame stay immutable; candidate root
// is an exclusive output lease. No application view changes until Verified.
// Keep this coordinator in owner storage, not on an ESP task stack. After an
// uncertain root write, reload the root pair before retrying with a parent view.
class SdIndexTransaction
{
  public:
    explicit SdIndexTransaction(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    uint8_t completedEntries() const { return completed_entries_; }
    bool begin(const ::geocaching::storage::IndexRootView& parent, unsigned parent_copy,
               ::geocaching::ByteView frame, uint64_t segment_first, uint32_t frame_offset,
               ::geocaching::storage::IndexRootBytes& candidate)
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexTransactionStep::Idle || parent_copy > 1 || !validIndexRoot(parent) ||
            parent.sequence == UINT64_MAX || parent.revision == UINT64_MAX) return false;
        const auto overlaps = [&](::geocaching::ByteView input)
        {
            const auto a = reinterpret_cast<uintptr_t>(input.data), b = reinterpret_cast<uintptr_t>(candidate.data());
            return a <= b ? b - a < input.size : a - b < candidate.size();
        };
        if (overlaps(frame) || overlaps(parent.shards) || !cursor_.open(frame, parent.sequence, segment_first, frame_offset)) return false;
        parent_ = parent;
        frame_ = frame;
        candidate_ = &candidate;
        target_copy_ = static_cast<uint8_t>(1 - parent_copy);
        segment_first_ = segment_first;
        frame_offset_ = frame_offset;
        std::memcpy(frame_crc_.data(), frame.data + 20, frame_crc_.size());
        parent_crc_ = ::sys::crc32(parent.shards.data, parent.shards.size);
        result_ = IndexTransactionStep::Working;
        return true;
    }
    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        return result_ == IndexTransactionStep::Verified &&
               ::geocaching::storage::decodeIndexRoot({candidate_->data(), candidate_->size()}, volume_, out);
    }
    IndexTransactionStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexTransactionStep::Working) return result_;
        if (phase_ == Phase::DirectoryVolume)
        {
            VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexTransactionStep::IoError);
            if (!(current == volume_)) return fail(IndexTransactionStep::VolumeChanged);
            directory_depth_ = 0;
            phase_ = Phase::DirectoryCheck;
            return result_;
        }
        if (phase_ == Phase::DirectoryCheck || phase_ == Phase::DirectoryCreate)
        {
            char path[64];
            if (directory_depth_ == 0) std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/index");
            else if (directory_depth_ == 1) std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/index/%c", parent_.slot);
            else std::snprintf(path, sizeof(path), "/trailmate/geocaching/.state/index/%c/%02x", parent_.slot, static_cast<unsigned>(entry_.table));
            if (phase_ == Phase::DirectoryCheck && !storage::sd_is_directory(path))
            {
                phase_ = Phase::DirectoryCreate;
                return result_;
            }
            if (phase_ == Phase::DirectoryCreate && !storage::sd_mkdir(path)) return fail(IndexTransactionStep::IoError);
            if (++directory_depth_ < 3) phase_ = Phase::DirectoryCheck;
            else
            {
                if (!startHead(0, baseline_)) return fail(IndexTransactionStep::Invalid);
                phase_ = Phase::InitializeFirst;
            }
            return result_;
        }
        if (phase_ == Phase::Next)
        {
            if (!cursor_.next(entry_))
            {
                TransactionIndexCursor verify;
                if (!cursor_.complete() || std::memcmp(frame_.data + 20, frame_crc_.data(), frame_crc_.size()) ||
                    ::sys::crc32(parent_.shards.data, parent_.shards.size) != parent_crc_ ||
                    !verify.open(frame_, parent_.sequence, segment_first_, frame_offset_)) return fail(IndexTransactionStep::Invalid);
                std::memcpy(candidate_->data() + 48, parent_.shards.data, kIndexShardBitmapSize);
                IndexedMutation changed;
                while (verify.next(changed))
                {
                    const auto bucket = ::sys::crc32(changed.key.data, changed.key.size) & 0xff;
                    (*candidate_)[48 + (changed.table - 1) * 32 + bucket / 8] |= static_cast<uint8_t>(1u << (bucket % 8));
                }
                auto next = parent_;
                ++next.sequence;
                ++next.revision;
                next.shards = {candidate_->data() + 48, kIndexShardBitmapSize};
                IndexRootView selected;
                if (!verify.complete() || !selectIndexRoot(parent_, next, selected) || !encodeIndexRoot(volume_, next, *candidate_)) return fail(IndexTransactionStep::Invalid);
                auto& writer = operation_.emplace<SdIndexRootWriter>(volume_);
                if (!writer.begin(target_copy_, *candidate_)) return fail(IndexTransactionStep::Invalid);
                phase_ = Phase::Root;
                return result_;
            }
            const auto bucket = static_cast<uint8_t>(::sys::crc32(entry_.key.data, entry_.key.size));
            baseline_ = {parent_.epoch, 0, 0, entry_.table, bucket};
            if (indexHasShard(parent_, entry_.table, bucket))
            {
                auto& reader = operation_.emplace<SdIndexHeadReader>(volume_, parent_.slot, parent_.epoch, parent_.sequence);
                if (!reader.begin(entry_.table, entry_.key)) return fail(IndexTransactionStep::Invalid);
                phase_ = Phase::ReadHead;
            }
            else
            {
                phase_ = Phase::DirectoryVolume;
            }
            return result_;
        }
        if (phase_ == Phase::ReadHead)
        {
            auto& reader = std::get<SdIndexHeadReader>(operation_);
            const auto status = reader.step();
            if (status == IndexHeadReadStep::Working) return result_;
            if (status != IndexHeadReadStep::Ready || !reader.selected(baseline_)) return error(status);
            head_copy_ = static_cast<uint8_t>(1 - reader.selectedCopy());
            return startAppend();
        }
        if (phase_ == Phase::InitializeFirst || phase_ == Phase::InitializeSecond || phase_ == Phase::Head)
        {
            const auto status = std::get<SdIndexHeadWriter>(operation_).step();
            if (status == IndexHeadWriteStep::Working) return result_;
            if (status != IndexHeadWriteStep::Verified) return error(status);
            if (phase_ == Phase::InitializeFirst)
            {
                if (!startHead(1, baseline_)) return fail(IndexTransactionStep::Invalid);
                phase_ = Phase::InitializeSecond;
                return result_;
            }
            if (phase_ == Phase::InitializeSecond)
            {
                head_copy_ = 1;
                return startAppend();
            }
            ++completed_entries_;
            operation_.emplace<std::monostate>();
            phase_ = Phase::Next;
            return result_;
        }
        if (phase_ == Phase::Append)
        {
            auto& writer = std::get<SdIndexAppend>(operation_);
            const auto status = writer.step();
            if (status == IndexAppendStep::Working) return result_;
            if (status != IndexAppendStep::Verified) return error(status);
            const auto length = writer.writtenLength();
            if (length <= baseline_.length) return fail(IndexTransactionStep::Invalid);
            auto next = baseline_;
            next.sequence = parent_.sequence + 1;
            next.length = length;
            if (!startHead(head_copy_, next)) return fail(IndexTransactionStep::Invalid);
            phase_ = Phase::Head;
            return result_;
        }
        const auto status = std::get<SdIndexRootWriter>(operation_).step();
        if (status == IndexRootWriteStep::Working) return result_;
        if (status != IndexRootWriteStep::Verified) return error(status);
        return result_ = IndexTransactionStep::Verified;
    }

  private:
    enum class Phase : uint8_t
    {
        Next,
        DirectoryVolume,
        DirectoryCheck,
        DirectoryCreate,
        ReadHead,
        InitializeFirst,
        InitializeSecond,
        Append,
        Head,
        Root
    };
    bool startHead(unsigned copy, const ::geocaching::storage::IndexShardHead& head)
    {
        return operation_.emplace<SdIndexHeadWriter>(volume_, parent_.slot).begin(entry_.key, copy, head);
    }
    IndexTransactionStep startAppend()
    {
        if (!operation_.emplace<SdIndexAppend>(volume_, parent_.slot).begin(entry_)) return fail(IndexTransactionStep::Invalid);
        phase_ = Phase::Append;
        return result_;
    }
    template <class Status>
    IndexTransactionStep error(Status status)
    {
        return fail(status == Status::VolumeChanged ? IndexTransactionStep::VolumeChanged : status == Status::IoError ? IndexTransactionStep::IoError
                                                                                                                      : IndexTransactionStep::Invalid);
    }
    IndexTransactionStep fail(IndexTransactionStep result)
    {
        operation_.emplace<std::monostate>();
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView parent_;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    ::geocaching::ByteView frame_;
    ::geocaching::storage::TransactionIndexCursor cursor_;
    ::geocaching::storage::IndexedMutation entry_;
    ::geocaching::storage::IndexShardHead baseline_;
    std::variant<std::monostate, SdIndexHeadReader, SdIndexHeadWriter, SdIndexAppend, SdIndexRootWriter> operation_;
    std::array<uint8_t, 4> frame_crc_{};
    uint64_t segment_first_ = 0;
    uint32_t frame_offset_ = 0, parent_crc_ = 0;
    uint8_t target_copy_ = 0, head_copy_ = 0;
    uint8_t completed_entries_ = 0;
    uint8_t directory_depth_ = 0;
    Phase phase_ = Phase::Next;
    IndexTransactionStep result_ = IndexTransactionStep::Idle;
};
static_assert(sizeof(SdIndexTransaction) <= 768, "Transaction coordination must overlay its operation workspaces");
} // namespace platform::esp::arduino_common::geocaching
