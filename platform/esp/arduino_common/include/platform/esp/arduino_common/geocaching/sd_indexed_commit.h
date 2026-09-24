#pragma once
#include "geocaching/storage/record_shape.h"
#include "platform/esp/arduino_common/geocaching/sd_index_references.h"
#include "platform/esp/arduino_common/geocaching/sd_index_transaction.h"
#include "platform/esp/arduino_common/geocaching/sd_journal.h"
#include "platform/esp/arduino_common/geocaching/sd_journal_segment.h"
#include <memory>

namespace platform::esp::arduino_common::geocaching
{
enum class IndexedCommitStep : uint8_t
{
    Idle,
    Working,
    Verified,
    Invalid,
    IoError,
    OutOfMemory,
    VolumeChanged,
    RecoveryRequired,
    NeedsValidation,
    Validated
};

// Caller prevalidates operation-specific business rules.
// Existing author issuance records cannot be changed or removed.
// Task/request/attempt references are checked here before journal creation.
// Mutation bytes stay immutable until inputConsumed(). The frame/output-root
// leases stay alive through completion. Store this coordinator off task stacks.
class SdIndexedCommit
{
  public:
    explicit SdIndexedCommit(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& parent, unsigned parent_copy,
               const ::geocaching::storage::MutationView* mutations, size_t count,
               uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate,
               bool validation_only = false, size_t required_absent = 0)
    {
        using namespace ::geocaching::storage;
        TransactionEncoding encoding;
        if (result_ != IndexedCommitStep::Idle || !validIndexRoot(parent) || parent_copy > 1 ||
            parent.sequence == UINT64_MAX || parent.revision == UINT64_MAX || !frame || capacity < 24 ||
            required_absent > count || !encoding.open(parent.sequence, mutations, count) || encoding.size() > capacity - 24) return false;
        for (size_t i = 0; i < count; ++i)
            if (!validStoredRowShape(mutations[i])) return false;
        if (!prepare(parent, parent_copy, frame, capacity, candidate)) return false;
        validation_only_ = validation_only;
        absent_count_ = required_absent;
        expected_size_ = encoding.size() + 24;
        bool references = false, authors = false;
        for (size_t i = 0; i < count; ++i)
        {
            references |= mutations[i].table == 5 || mutations[i].table == 10 || mutations[i].table == 13;
            authors |= mutations[i].table == 3;
        }
        if ((references || authors || required_absent) && encoding.inputOverlaps({frame, capacity})) return false;
        mutations_ = mutations;
        mutation_count_ = count;
        if (references)
        {
            references_.reset(new (std::nothrow) SdIndexReferences(volume_));
            if (!references_)
            {
                fail(IndexedCommitStep::OutOfMemory);
                return false;
            }
            if (!references_->begin(parent, frame, capacity, mutations, count))
            {
                fail(IndexedCommitStep::Invalid);
                return false;
            }
            mutations_ = mutations;
            mutation_count_ = count;
            phase_ = Phase::References;
            result_ = IndexedCommitStep::Working;
            return true;
        }
        if (authors || required_absent)
        {
            phase_ = Phase::Absent;
            result_ = IndexedCommitStep::Working;
            return true;
        }
        if (validation_only_)
        {
            phase_ = Phase::ValidationVolume;
            result_ = IndexedCommitStep::Working;
            return true;
        }
        auto& journal = operation_.emplace<SdGeocachingJournal>(volume_);
        const auto started = journal.begin(parent.sequence, mutations, count);
        if (started != JournalWriteResult::InProgress)
        {
            fail(IndexedCommitStep::Invalid);
            return false;
        }
        captured_ = journal.captureReadback(frame, capacity);
        result_ = IndexedCommitStep::Working;
        return true;
    }
    bool resume(const ::geocaching::storage::IndexRootView& parent, unsigned parent_copy,
                uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate)
    {
        if (!prepare(parent, parent_copy, frame, capacity, candidate)) return false;
        recovering_ = consumed_ = true;
        operation_.emplace<SdJournalSegment>();
        phase_ = Phase::CheckVolume;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    ::geocaching::ByteView recoveredFrame() const
    {
        return result_ == IndexedCommitStep::NeedsValidation ? ::geocaching::ByteView{frame_, expected_size_} : ::geocaching::ByteView{};
    }
    bool validateRecovered(bool valid)
    {
        if (result_ != IndexedCommitStep::NeedsValidation) return false;
        if (!valid || !recoveredUnchanged())
        {
            fail(IndexedCommitStep::Invalid);
            return true;
        }
        phase_ = Phase::StartIndex;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    bool inputConsumed() const { return consumed_; }
    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        if (result_ != IndexedCommitStep::Verified) return false;
        out = committed_;
        return true;
    }
    IndexedCommitStep step()
    {
        if (result_ != IndexedCommitStep::Working) return result_;
        if (phase_ == Phase::ValidationVolume)
        {
            ::geocaching::storage::VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexedCommitStep::IoError);
            if (current != volume_) return fail(IndexedCommitStep::VolumeChanged);
            return fail(IndexedCommitStep::Validated);
        }
        if (phase_ == Phase::References)
        {
            const auto status = references_->step();
            if (status == IndexScanStep::Working) return result_;
            if (status != IndexScanStep::End)
                return fail(status == IndexScanStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexScanStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                         : IndexedCommitStep::Invalid);
            references_.reset();
            phase_ = Phase::Absent;
            return result_;
        }
        if (phase_ == Phase::Absent)
        {
            if (absent_position_ == absent_count_)
            {
                phase_ = Phase::Authors;
                return result_;
            }
            if (!absent_pending_)
            {
                const auto& row = mutations_[absent_position_];
                if (!operation_.emplace<SdIndexGet>(volume_).begin(parent_, row.table, row.key, frame_, capacity_))
                    return fail(IndexedCommitStep::Invalid);
                absent_pending_ = true;
                return result_;
            }
            const auto status = std::get<SdIndexGet>(operation_).step();
            if (status == IndexGetStep::Working) return result_;
            if (status != IndexGetStep::NotFound)
                return fail(status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexGetStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                       : IndexedCommitStep::Invalid);
            operation_.emplace<std::monostate>();
            absent_pending_ = false;
            ++absent_position_;
            return result_;
        }
        if (phase_ == Phase::Authors)
        {
            if (author_pending_)
            {
                auto& read = std::get<SdIndexGet>(operation_);
                const auto status = read.step();
                if (status == IndexGetStep::Working) return result_;
                if (status == IndexGetStep::Ready)
                {
                    const auto prior = read.value();
                    const auto& row = mutations_[author_position_];
                    if (row.erase || prior.size != row.value.size || std::memcmp(prior.data, row.value.data, prior.size))
                        return fail(IndexedCommitStep::Invalid);
                }
                else if (status != IndexGetStep::NotFound)
                    return fail(status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexGetStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                           : IndexedCommitStep::Invalid);
                operation_.emplace<std::monostate>();
                author_pending_ = false;
                ++author_position_;
                return result_;
            }
            while (author_position_ < mutation_count_ && mutations_[author_position_].table != 3) ++author_position_;
            if (author_position_ < mutation_count_)
            {
                if (!operation_.emplace<SdIndexGet>(volume_).begin(parent_, 3, mutations_[author_position_].key, frame_, capacity_))
                    return fail(IndexedCommitStep::Invalid);
                author_pending_ = true;
                return result_;
            }
            if (validation_only_)
            {
                phase_ = Phase::ValidationVolume;
                return result_;
            }
            auto& journal = operation_.emplace<SdGeocachingJournal>(volume_);
            if (journal.begin(parent_.sequence, mutations_, mutation_count_) != JournalWriteResult::InProgress)
                return fail(IndexedCommitStep::Invalid);
            captured_ = journal.captureReadback(frame_, capacity_);
            phase_ = Phase::Journal;
            return result_;
        }
        if (phase_ == Phase::CheckVolume || phase_ == Phase::CheckRecoveredVolume)
        {
            ::geocaching::storage::VolumeInstance current;
            if (inspectSdVolume(current) != SdVolumeResult::Ready) return fail(IndexedCommitStep::RecoveryRequired);
            if (current != volume_) return fail(IndexedCommitStep::VolumeChanged);
            if (phase_ == Phase::CheckRecoveredVolume) return result_ = IndexedCommitStep::NeedsValidation;
            phase_ = Phase::Open;
            return result_;
        }
        if (phase_ == Phase::Journal)
        {
            auto& journal = std::get<SdGeocachingJournal>(operation_);
            const auto status = journal.step();
            if (status == JournalWriteResult::InProgress) return result_;
            if (status != JournalWriteResult::Verified)
            {
                if (status == JournalWriteResult::VolumeChanged) return fail(IndexedCommitStep::VolumeChanged);
                return fail(journal.mayHaveWritten() || status == JournalWriteResult::Exists ? IndexedCommitStep::RecoveryRequired : IndexedCommitStep::IoError);
            }
            consumed_ = true;
            if (captured_)
            {
                if (!operation_.emplace<SdIndexTransaction>(volume_).begin(parent_, parent_copy_, {frame_, expected_size_}, parent_.sequence + 1, 0, *candidate_))
                    return fail(IndexedCommitStep::RecoveryRequired);
                phase_ = Phase::Index;
                return result_;
            }
            operation_.emplace<SdJournalSegment>();
            phase_ = Phase::Open;
            return result_;
        }
        if (phase_ == Phase::Open)
        {
            if (!std::get<SdJournalSegment>(operation_).open(parent_.sequence + 1)) return fail(IndexedCommitStep::RecoveryRequired);
            phase_ = Phase::Read;
            return result_;
        }
        if (phase_ == Phase::Read)
        {
            ::geocaching::storage::RecordFrameView frame;
            const auto status = std::get<SdJournalSegment>(operation_).next(frame_, capacity_, frame);
            if (status == SegmentReadResult::InProgress) return result_;
            if (status != SegmentReadResult::Record || frame.sequence != parent_.sequence + 1 ||
                (expected_size_ && frame.payload.size + 24 != expected_size_))
                return fail(IndexedCommitStep::RecoveryRequired);
            expected_size_ = frame.payload.size + 24;
            if (!::geocaching::storage::validTransactionRowShapes(frame.payload, parent_.sequence)) return fail(IndexedCommitStep::RecoveryRequired);
            if (recovering_) std::memcpy(recovery_crc_.data(), frame_ + 20, recovery_crc_.size());
            phase_ = recovering_ ? Phase::CheckRecoveredVolume : Phase::StartIndex;
            return result_;
        }
        if (phase_ == Phase::StartIndex)
        {
            if (recovering_ && !recoveredUnchanged()) return fail(IndexedCommitStep::Invalid);
            if (!operation_.emplace<SdIndexTransaction>(volume_).begin(parent_, parent_copy_, {frame_, expected_size_}, parent_.sequence + 1, 0, *candidate_))
                return fail(IndexedCommitStep::RecoveryRequired);
            phase_ = Phase::Index;
            return result_;
        }
        auto& index = std::get<SdIndexTransaction>(operation_);
        const auto status = index.step();
        if (status == IndexTransactionStep::Working) return result_;
        if (status == IndexTransactionStep::VolumeChanged) return fail(IndexedCommitStep::VolumeChanged);
        if (status != IndexTransactionStep::Verified || !index.committed(committed_)) return fail(IndexedCommitStep::RecoveryRequired);
        operation_.emplace<std::monostate>();
        result_ = IndexedCommitStep::Verified;
        return result_;
    }

  private:
    bool recoveredUnchanged() const
    {
        ::geocaching::storage::RecordFrameView decoded;
        return expected_size_ >= 24 && !std::memcmp(frame_ + 20, recovery_crc_.data(), recovery_crc_.size()) &&
               ::geocaching::storage::decodeRecordFrame({frame_, expected_size_}, decoded) && decoded.sequence == parent_.sequence + 1 &&
               ::geocaching::storage::validTransactionRowShapes(decoded.payload, parent_.sequence);
    }
    bool prepare(const ::geocaching::storage::IndexRootView& parent, unsigned parent_copy, uint8_t* frame, size_t capacity,
                 ::geocaching::storage::IndexRootBytes& candidate)
    {
        if (result_ != IndexedCommitStep::Idle || !::geocaching::storage::validIndexRoot(parent) || parent_copy > 1 ||
            parent.sequence == UINT64_MAX || parent.revision == UINT64_MAX || !frame || capacity < 24) return false;
        const auto overlap = [](::geocaching::ByteView a, ::geocaching::ByteView b)
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data), y = reinterpret_cast<uintptr_t>(b.data);
            return x <= y ? y - x < a.size : x - y < b.size;
        };
        if (overlap({frame, capacity}, {candidate.data(), candidate.size()}) || overlap(parent.shards, {frame, capacity}) ||
            overlap(parent.shards, {candidate.data(), candidate.size()})) return false;
        parent_ = parent;
        parent_copy_ = static_cast<uint8_t>(parent_copy);
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        return true;
    }
    enum class Phase : uint8_t
    {
        References,
        Absent,
        Authors,
        ValidationVolume,
        Journal,
        CheckVolume,
        Open,
        Read,
        CheckRecoveredVolume,
        StartIndex,
        Index
    };
    IndexedCommitStep fail(IndexedCommitStep result)
    {
        references_.reset();
        operation_.emplace<std::monostate>();
        consumed_ = true;
        return result_ = result;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView parent_, committed_;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, expected_size_ = 0;
    std::unique_ptr<SdIndexReferences> references_;
    const ::geocaching::storage::MutationView* mutations_ = nullptr;
    size_t mutation_count_ = 0;
    std::variant<std::monostate, SdGeocachingJournal, SdJournalSegment, SdIndexTransaction, SdIndexGet> operation_;
    size_t author_position_ = 0;
    bool author_pending_ = false;
    size_t absent_count_ = 0, absent_position_ = 0;
    bool absent_pending_ = false;
    uint8_t parent_copy_ = 0;
    bool consumed_ = false;
    bool captured_ = false;
    bool recovering_ = false;
    bool validation_only_ = false;
    std::array<uint8_t, 4> recovery_crc_{};
    Phase phase_ = Phase::Journal;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedCommit) <= 1024, "Commit coordination overlays its I/O workspaces");
} // namespace platform::esp::arduino_common::geocaching
