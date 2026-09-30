#pragma once
#include "geocaching/protocol/directory_reply.h"
#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/publish_response.h"
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/queued_request.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"

namespace platform::esp::arduino_common::geocaching
{
// Authenticated endpoints form key. Response and encoding workspace are caller
// leases; no response-sized object is stored in this coordinator.
class SdIndexedDirectoryReply
{
  public:
    explicit SdIndexedDirectoryReply(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy, ::geocaching::ByteView key,
               uint8_t operation, ::geocaching::ByteView response,
               ::geocaching::storage::QueuedRequestWorkspace& workspace, uint8_t* frame, size_t capacity,
               ::geocaching::storage::IndexRootBytes& candidate,
               ::geocaching::protocol::RecordCrypto* crypto = nullptr, uint8_t* verification = nullptr, size_t verification_capacity = 0)
    {
        using namespace ::geocaching;
        if (result_ != IndexedCommitStep::Idle || key.size != key_.size() || !key.data || copy > 1 ||
            operation > 2 || !response.data || response.size > kMaxApplicationBytes ||
            (operation == 1 && (!crypto || !verification || !verification_capacity || response.size > 512))) return false;
        const auto overlaps = [](ByteView a, ByteView b)
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data), y = reinterpret_cast<uintptr_t>(b.data);
            return a.size && b.size && (x <= y ? y - x < a.size : x - y < b.size);
        };
        const ByteView buffers[] = {{workspace.outgoing, workspace.outgoing_capacity}, {workspace.task.data(), workspace.task.size()}, {frame, capacity}, {candidate.data(), candidate.size()}, root.shards};
        for (size_t i = 0; i < 5; ++i)
        {
            if (overlaps(response, buffers[i])) return false;
            if (operation == 1 && overlaps({verification, verification_capacity}, buffers[i])) return false;
            for (size_t j = 0; j < i; ++j)
                if (overlaps(buffers[i], buffers[j])) return false;
        }
        if (operation == 1 && overlaps({verification, verification_capacity}, response)) return false;
        crypto_ = crypto;
        verification_ = verification;
        verification_capacity_ = verification_capacity;
        std::memcpy(key_.data(), key.data, key_.size());
        root_ = root;
        copy_ = copy;
        response_ = response;
        operation_code_ = operation;
        workspace_ = &workspace;
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 5, {key_.data(), key_.size()}, frame_, capacity_)) return false;
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
        if (phase_ == Phase::NextChild) return nextChild();
        auto& read = std::get<SdIndexGet>(io_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready)
            return fail(status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexGetStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                   : IndexedCommitStep::Invalid);
        if (phase_ == Phase::Outgoing)
        {
            OutgoingView outgoing;
            const ByteView key{key_.data(), key_.size()};
            if (!decodeOutgoing(key, read.value(), outgoing)) return fail(IndexedCommitStep::Invalid);
            RequestId id;
            std::memcpy(id.bytes.data(), key_.data() + 32, 16);
            protocol::DirectoryCapabilities capabilities;
            bool valid = operation_code_ == 0
                             ? protocol::decodeDirectoryCapabilities(response_, id, capabilities) && protocol::matchesCapabilitiesRequest(outgoing.request, id, response_.size)
                             : protocol::matchesQueryReply(outgoing.request, response_, id);
            if (operation_code_ == 1)
            {
                protocol::PublishRequestView request;
                protocol::VerifiedRecordView record;
                protocol::PublishDisposition disposition;
                valid = protocol::decodePublishRequest(outgoing.request, id, request) && response_.size <= request.budget &&
                        protocol::verifyGeocache(request.signed_cache, *crypto_, verification_, verification_capacity_, record) == protocol::VerificationResult::Valid &&
                        protocol::decodePublishResponse(response_, id, record.id, record.hash, record.record.revision, record.record.state, disposition);
                if (valid)
                {
                    cache_ = record.id;
                    hash_ = record.hash;
                }
            }
            if (!valid) return fail(IndexedCommitStep::Invalid);
            duplicate_ = outgoing.state == 4;
            if (duplicate_ && (outgoing.terminal_data.size != response_.size ||
                               std::memcmp(outgoing.terminal_data.data, response_.data, response_.size))) return fail(IndexedCommitStep::Invalid);
            if (!duplicate_ && !outgoing.continue_intent) return fail(IndexedCommitStep::Invalid);
            std::memcpy(task_id_.data(), outgoing.task_id.data, task_id_.size());
            outgoing.state = 4;
            outgoing.terminal_data = response_;
            if (!encodeOutgoing(key, outgoing, workspace_->outgoing, workspace_->outgoing_capacity, outgoing_size_)) return fail(IndexedCommitStep::Invalid);
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 10, {task_id_.data(), task_id_.size()}, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
            phase_ = Phase::Task;
            return result_;
        }
        if (phase_ == Phase::Task)
        {
            TaskView task;
            OutgoingView outgoing;
            const ByteView task_key{task_id_.data(), task_id_.size()}, key{key_.data(), key_.size()};
            if (!decodeTask(task_key, read.value(), task) || task.kind != (operation_code_ == 1 ? 1 : 3) ||
                !decodeOutgoing(key, {workspace_->outgoing, outgoing_size_}, outgoing) ||
                !requestBelongsToTask(task_key, task, key, outgoing)) return fail(IndexedCommitStep::Invalid);
            if (operation_code_ == 1 && (task.cache_id.size != 32 || task.revision_hash.size != 32 ||
                                         std::memcmp(task.cache_id.data, cache_.bytes.data(), 32) ||
                                         std::memcmp(task.revision_hash.data, hash_.bytes.data(), 32))) return fail(IndexedCommitStep::Invalid);
            if (duplicate_) return fail(IndexedCommitStep::Verified);
            if (!task.continue_intent || task.state == 5 || read.value().size > workspace_->task.size()) return fail(IndexedCommitStep::Invalid);
            task_size_ = read.value().size;
            std::memcpy(workspace_->task.data(), read.value().data, task_size_);
            phase_ = Phase::NextChild;
            return result_;
        }
        OutgoingView child;
        if (!decodeOutgoing({child_key_.data(), child_key_.size()}, read.value(), child)) return fail(IndexedCommitStep::Invalid);
        all_confirmed_ = all_confirmed_ && child.state == 4;
        phase_ = Phase::NextChild;
        return result_;
    }

  private:
    enum class Phase : uint8_t
    {
        Outgoing,
        Task,
        NextChild,
        Child,
        Commit
    };
    IndexedCommitStep nextChild()
    {
        using namespace ::geocaching::storage;
        TaskView task;
        const ::geocaching::ByteView task_key{task_id_.data(), task_id_.size()};
        if (!decodeTask(task_key, {workspace_->task.data(), task_size_}, task)) return fail(IndexedCommitStep::Invalid);
        while (child_ < task.request_count)
        {
            const auto key = task.requests[child_++];
            if (!std::memcmp(key.data, key_.data(), key_.size())) continue;
            std::memcpy(child_key_.data(), key.data, child_key_.size());
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 5, {child_key_.data(), child_key_.size()}, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
            phase_ = Phase::Child;
            return result_;
        }
        task.state = all_confirmed_ ? 3 : 2;
        size_t size = 0;
        // Encode into the now-free read frame, then retain only the small task
        // encoding in its caller workspace before journal readback reuses frame.
        if (!encodeTask(task_key, task, frame_, capacity_, size) || size > workspace_->task.size()) return fail(IndexedCommitStep::Invalid);
        std::memcpy(workspace_->task.data(), frame_, size);
        mutations_[0] = {5, {key_.data(), key_.size()}, {workspace_->outgoing, outgoing_size_}, false};
        mutations_[1] = {10, task_key, {workspace_->task.data(), size}, false};
        if (!io_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, mutations_.data(), mutations_.size(), frame_, capacity_, *candidate_))
            return fail(IndexedCommitStep::Invalid);
        phase_ = Phase::Commit;
        return result_;
    }
    IndexedCommitStep fail(IndexedCommitStep status)
    {
        io_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    ::geocaching::ByteView response_;
    ::geocaching::protocol::RecordCrypto* crypto_ = nullptr;
    ::geocaching::GeocacheId cache_;
    ::geocaching::RevisionHash hash_;
    uint8_t* verification_ = nullptr;
    size_t verification_capacity_ = 0;
    ::geocaching::storage::QueuedRequestWorkspace* workspace_ = nullptr;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    std::array<::geocaching::storage::MutationView, 2> mutations_{};
    std::array<uint8_t, 48> key_{}, child_key_{};
    std::array<uint8_t, 16> task_id_{};
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, outgoing_size_ = 0, task_size_ = 0, child_ = 0;
    unsigned copy_ = 0;
    uint8_t operation_code_ = 0;
    bool duplicate_ = false, all_confirmed_ = true;
    std::variant<std::monostate, SdIndexGet, SdIndexedCommit> io_;
    Phase phase_ = Phase::Outgoing;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedDirectoryReply) <= 1536, "Reply coordination owns metadata, not response bytes");
} // namespace platform::esp::arduino_common::geocaching
