#pragma once
#include "geocaching/protocol/get_request.h"
#include "geocaching/protocol/get_response.h"
#include "geocaching/storage/installable_record.h"
#include "geocaching/storage/queued_request.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_download_context.h"

namespace platform::esp::arduino_common::geocaching
{
// Caller pins exact authenticated-source response bytes through inputConsumed.
// Signature verification, lookup and commit use separate explicit buffer leases.
class SdIndexedDownloadReply
{
  public:
    SdIndexedDownloadReply(const ::geocaching::storage::VolumeInstance& volume, ::geocaching::protocol::RecordCrypto& crypto)
        : volume_(volume), crypto_(crypto) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView key,
               uint64_t generation, ::geocaching::ByteView response, ::geocaching::storage::QueuedRequestWorkspace& workspace,
               uint8_t* frame, size_t capacity, uint8_t* verify, size_t verify_capacity, ::geocaching::storage::IndexRootBytes& candidate)
    {
        using ::geocaching::ByteView;
        if (result_ != IndexedCommitStep::Idle || copy > 1 || !key.data || key.size != key_.size() || !response.data ||
            response.size > ::geocaching::kMaxApplicationBytes || !verify || !verify_capacity) return false;
        const ByteView leases[] = {response, {workspace.outgoing, workspace.outgoing_capacity}, {workspace.task.data(), workspace.task.size()}, {frame, capacity}, {verify, verify_capacity}, {candidate.data(), candidate.size()}, root.shards};
        for (size_t i = 0; i < 7; ++i)
            for (size_t j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(leases[i].data), b = reinterpret_cast<uintptr_t>(leases[j].data);
                if (leases[i].size && leases[j].size && (a <= b ? b - a < leases[i].size : a - b < leases[j].size)) return false;
            }
        std::memcpy(key_.data(), key.data, key_.size());
        root_ = root;
        copy_ = copy;
        response_ = response;
        workspace_ = &workspace;
        frame_ = frame;
        capacity_ = capacity;
        verify_ = verify;
        verify_capacity_ = verify_capacity;
        candidate_ = &candidate;
        if (!io_.emplace<SdIndexedDownloadContext>(volume_).begin(root_, {key_.data(), key_.size()}, generation, frame_, capacity_)) return false;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    bool inputConsumed() const
    {
        return result_ != IndexedCommitStep::Working || (phase_ == Phase::Commit && std::get<SdIndexedCommit>(io_).inputConsumed());
    }
    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        if (result_ != IndexedCommitStep::Verified) return false;
        if (duplicate_)
        {
            out = root_;
            return true;
        }
        return std::get<SdIndexedCommit>(io_).committed(out);
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (phase_ == Phase::Commit) return result_ = std::get<SdIndexedCommit>(io_).step();
        if (phase_ == Phase::Context)
        {
            auto& context = std::get<SdIndexedDownloadContext>(io_);
            const auto status = context.step();
            if (status == IndexGetStep::Working) return result_;
            if (status != IndexGetStep::Ready) return readError(status);
            OutgoingView outgoing;
            TaskView task;
            CacheHeadView head;
            if (!context.view(outgoing, task, head)) return fail(IndexedCommitStep::Invalid);
            RequestId id;
            std::memcpy(id.bytes.data(), key_.data() + 32, 16);
            protocol::GetRequestView requested;
            protocol::GetResponseView reply;
            if (!protocol::decodeGetRequest(outgoing.request, id, requested) ||
                !protocol::decodeGetResponse(response_, id, requested.budget, reply) || reply.has_conflict) return fail(IndexedCommitStep::Invalid);
            GeocacheId cache_id;
            RevisionHash hash;
            std::memcpy(cache_id.bytes.data(), task.cache_id.data, 32);
            std::memcpy(hash.bytes.data(), task.revision_hash.data, 32);
            if (protocol::verifyGeocache(reply.signed_cache, crypto_, verify_, verify_capacity_, verified_, &cache_id, &hash) != protocol::VerificationResult::Valid ||
                verified_.record.revision < head.highest_seen_revision || head.conflict_state == 2) return fail(IndexedCommitStep::Invalid);
            duplicate_ = outgoing.state == 4;
            if (duplicate_ && (outgoing.terminal_data.size != response_.size || std::memcmp(outgoing.terminal_data.data, response_.data, response_.size)))
                return fail(IndexedCommitStep::Invalid);
            if (!duplicate_)
            {
                if (!context.intentActive()) return fail(IndexedCommitStep::Invalid);
                std::memcpy(task_id_.data(), outgoing.task_id.data, task_id_.size());
                outgoing.state = 4;
                outgoing.terminal_data = response_;
                task.state = 1;
                size_t outgoing_size = 0, task_size = 0;
                if (!encodeOutgoing({key_.data(), key_.size()}, outgoing, workspace_->outgoing, workspace_->outgoing_capacity, outgoing_size) ||
                    !encodeTask({task_id_.data(), task_id_.size()}, task, workspace_->task.data(), workspace_->task.size(), task_size)) return fail(IndexedCommitStep::Invalid);
                mutations_[0] = {5, {key_.data(), key_.size()}, {workspace_->outgoing, outgoing_size}, false};
                mutations_[1] = {10, {task_id_.data(), task_id_.size()}, {workspace_->task.data(), task_size}, false};
            }
            if (!head.current_hash.size) return installableRecord(head, nullptr, verified_) ? commit() : fail(IndexedCommitStep::Invalid);
            if (!encodeCacheHead(task.cache_id, head, head_.data(), head_.size(), head_size_)) return fail(IndexedCommitStep::Invalid);
            std::memcpy(current_hash_.data(), head.current_hash.data, current_hash_.size());
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 1, {current_hash_.data(), current_hash_.size()}, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
            phase_ = Phase::Object;
            return result_;
        }
        auto& read = std::get<SdIndexGet>(io_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready) return readError(status);
        ObjectRefView object;
        CacheHeadView head;
        if (!decodeObjectRef({current_hash_.data(), current_hash_.size()}, read.value(), object) ||
            !decodeCacheHead({verified_.id.bytes.data(), 32}, {head_.data(), head_size_}, head) || !installableRecord(head, &object, verified_))
            return fail(IndexedCommitStep::Invalid);
        return commit();
    }

  private:
    enum class Phase : uint8_t
    {
        Context,
        Object,
        Commit
    };
    IndexedCommitStep commit()
    {
        if (duplicate_) return fail(IndexedCommitStep::Verified);
        if (!io_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, mutations_.data(), mutations_.size(), frame_, capacity_, *candidate_)) return fail(IndexedCommitStep::Invalid);
        phase_ = Phase::Commit;
        return result_;
    }
    IndexedCommitStep readError(IndexGetStep status)
    {
        return fail(status == IndexGetStep::IoError ? IndexedCommitStep::IoError : status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged
                                                                                                                         : IndexedCommitStep::Invalid);
    }
    IndexedCommitStep fail(IndexedCommitStep status)
    {
        io_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::protocol::VerifiedRecordView verified_;
    ::geocaching::ByteView response_;
    ::geocaching::storage::QueuedRequestWorkspace* workspace_ = nullptr;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    std::array<::geocaching::storage::MutationView, 2> mutations_{};
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 16> task_id_{};
    std::array<uint8_t, 32> current_hash_{};
    std::array<uint8_t, 64> head_{};
    uint8_t *frame_ = nullptr, *verify_ = nullptr;
    size_t capacity_ = 0, verify_capacity_ = 0, head_size_ = 0;
    unsigned copy_ = 0;
    bool duplicate_ = false;
    std::variant<std::monostate, SdIndexedDownloadContext, SdIndexGet, SdIndexedCommit> io_;
    Phase phase_ = Phase::Context;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedDownloadReply) <= 2048, "Download receipt owns metadata, not response-sized buffers");
} // namespace platform::esp::arduino_common::geocaching
