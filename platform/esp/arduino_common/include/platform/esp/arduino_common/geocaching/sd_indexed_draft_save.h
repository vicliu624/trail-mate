#pragma once
#include "geocaching/storage/draft_publication.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"

namespace platform::esp::arduino_common::geocaching
{
// One serialized owner pins the root, input encoding and frame until terminal.
// No draft list or text copy is owned. Keep this operation off ESP task stacks.
class SdIndexedDraftSave
{
  public:
    explicit SdIndexedDraftSave(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView key,
               ::geocaching::ByteView encoded, uint64_t expected_generation, uint8_t* frame, size_t capacity,
               ::geocaching::storage::IndexRootBytes& candidate)
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Idle || copy > 1 || !decodeDraft(key, encoded, next_) ||
            expected_generation == UINT64_MAX || next_.generation != expected_generation + 1) return false;
        const auto overlaps = [](::geocaching::ByteView a, ::geocaching::ByteView b)
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data), y = reinterpret_cast<uintptr_t>(b.data);
            return a.size && b.size && (x <= y ? y - x < a.size : x - y < b.size);
        };
        if (overlaps(encoded, {frame, capacity}) || overlaps(encoded, {candidate.data(), candidate.size()}) ||
            overlaps({frame, capacity}, {candidate.data(), candidate.size()}) || overlaps(root.shards, {candidate.data(), candidate.size()})) return false;
        std::memcpy(key_.data(), key.data, key_.size());
        mutation_ = {4, {key_.data(), key_.size()}, encoded, false};
        root_ = root;
        copy_ = copy;
        expected_ = expected_generation;
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        if (!operation_.emplace<SdIndexGet>(volume_).begin(root_, 4, mutation_.key, frame_, capacity_)) return false;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    bool inputConsumed() const
    {
        return result_ != IndexedCommitStep::Working ||
               (phase_ == Phase::Commit && std::get<SdIndexedCommit>(operation_).inputConsumed());
    }
    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        return result_ == IndexedCommitStep::Verified && std::get<SdIndexedCommit>(operation_).committed(out);
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (phase_ == Phase::Previous)
        {
            auto& read = std::get<SdIndexGet>(operation_);
            const auto status = read.step();
            if (status == IndexGetStep::Working) return result_;
            if (status != IndexGetStep::Ready && status != IndexGetStep::NotFound) return readFailure(status);
            DraftView previous;
            if (status == IndexGetStep::Ready && !decodeDraft(mutation_.key, read.value(), previous)) return fail(IndexedCommitStep::Invalid);
            const auto check = checkDraftUpdate(status == IndexGetStep::Ready ? &previous : nullptr, next_, expected_);
            if (check == DraftUpdateCheck::Allowed) return startCommit();
            if (check != DraftUpdateCheck::NeedsRetainedPublication) return fail(IndexedCommitStep::Invalid);
            scan_.reset(new (std::nothrow) SdIndexScan(volume_));
            if (!scan_ || !scan_->begin(root_, 5, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
            operation_.emplace<std::monostate>();
            phase_ = Phase::Requests;
            return result_;
        }
        if (phase_ == Phase::Requests)
        {
            const auto status = scan_->step();
            if (status == IndexScanStep::Working) return result_;
            if (status != IndexScanStep::Item)
                return fail(status == IndexScanStep::IoError ? IndexedCommitStep::IoError : status == IndexScanStep::VolumeChanged ? IndexedCommitStep::VolumeChanged
                                                                                                                                   : IndexedCommitStep::Invalid);
            MutationView row;
            OutgoingView outgoing;
            if (!scan_->item(row) || !decodeOutgoing(row.key, row.value, outgoing)) return fail(IndexedCommitStep::Invalid);
            RequestId request;
            std::memcpy(request.bytes.data(), row.key.data + 32, 16);
            protocol::PublishRequestView publish;
            bool matches = false;
            if (next_.author.size == 64 && protocol::decodePublishRequest(outgoing.request, request, publish))
            {
                protocol::CmpReader reader(publish.signed_cache);
                size_t fields = 0;
                ByteView encoded, signature;
                RecordView record;
                matches = reader.array(fields, 2) && fields == 2 && reader.binary(encoded, kMaxRecordBytes) &&
                          reader.binary(signature, 64) && reader.finished() && protocol::decodeGeocacheRecord(encoded, record) &&
                          !std::memcmp(record.author_public_key.data, next_.author.data, 64) &&
                          !std::memcmp(record.creation_nonce.data, key_.data(), 16);
            }
            if (matches)
            {
                std::memcpy(request_key_.data(), row.key.data, request_key_.size());
                std::memcpy(task_key_.data(), outgoing.task_id.data, task_key_.size());
                if (!operation_.emplace<SdIndexGet>(volume_).begin(root_, 10, outgoing.task_id, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
                phase_ = Phase::Task;
            }
            if (!scan_->advance()) return fail(IndexedCommitStep::Invalid);
            return result_;
        }
        if (phase_ == Phase::Task)
        {
            auto& read = std::get<SdIndexGet>(operation_);
            const auto status = read.step();
            if (status == IndexGetStep::Working) return result_;
            if (status != IndexGetStep::Ready) return readFailure(status);
            TaskView task;
            if (!decodeTask({task_key_.data(), task_key_.size()}, read.value(), task)) return fail(IndexedCommitStep::Invalid);
            bool linked = false;
            for (size_t i = 0; i < task.request_count; ++i)
                linked |= !std::memcmp(task.requests[i].data, request_key_.data(), request_key_.size());
            if (linked && task.kind == 1 && task.cache_id.size == 32 && task.revision_hash.size == 32 &&
                !std::memcmp(task.revision_hash.data, next_.base_hash.data, 32)) return startCommit();
            operation_.emplace<std::monostate>();
            phase_ = Phase::Requests;
            return result_;
        }
        return result_ = std::get<SdIndexedCommit>(operation_).step();
    }

  private:
    enum class Phase : uint8_t
    {
        Previous,
        Requests,
        Task,
        Commit
    };
    IndexedCommitStep startCommit()
    {
        scan_.reset();
        if (!operation_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, &mutation_, 1, frame_, capacity_, *candidate_))
            return fail(IndexedCommitStep::Invalid);
        phase_ = Phase::Commit;
        return result_;
    }
    IndexedCommitStep readFailure(IndexGetStep status)
    {
        return fail(status == IndexGetStep::IoError ? IndexedCommitStep::IoError : status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged
                                                                                                                         : IndexedCommitStep::Invalid);
    }
    IndexedCommitStep fail(IndexedCommitStep status)
    {
        scan_.reset();
        operation_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::storage::DraftView next_;
    ::geocaching::storage::MutationView mutation_;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    std::array<uint8_t, 16> key_{};
    std::array<uint8_t, 16> task_key_{};
    std::array<uint8_t, 48> request_key_{};
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    uint64_t expected_ = 0;
    unsigned copy_ = 0;
    std::variant<std::monostate, SdIndexGet, SdIndexedCommit> operation_;
    std::unique_ptr<SdIndexScan> scan_;
    Phase phase_ = Phase::Previous;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedDraftSave) <= 1536, "Draft save owns metadata, not draft text");
} // namespace platform::esp::arduino_common::geocaching
