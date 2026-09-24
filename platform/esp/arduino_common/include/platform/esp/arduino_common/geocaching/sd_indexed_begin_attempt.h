#pragma once
#include "geocaching/storage/cache_head.h"
#include "geocaching/storage/queued_request.h"
#include "geocaching/storage/tx_attempt.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"

namespace platform::esp::arduino_common::geocaching
{
// Transport may run only after Verified and after reloading the committed
// request. Caller owns the encoding/read leases; no payload array lives here.
class SdIndexedBeginAttempt
{
  public:
    explicit SdIndexedBeginAttempt(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy,
               const ::geocaching::Destination& local, ::geocaching::ByteView request_key,
               const std::array<uint8_t, 16>& attempt_id, const ::geocaching::storage::StoredTime& submitted,
               ::geocaching::storage::QueuedRequestWorkspace& workspace,
               uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate)
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Idle || copy > 1 || !request_key.data || request_key.size != 48 ||
            std::memcmp(request_key.data, local.bytes.data(), 16) || !workspace.outgoing) return false;
        const ByteView leases[] = {{workspace.outgoing, workspace.outgoing_capacity}, {workspace.task.data(), workspace.task.size()}, {frame, capacity}, {candidate.data(), candidate.size()}, root.shards};
        for (size_t i = 0; i < 5; ++i)
            for (size_t j = 0; j < i; ++j)
            {
                const auto a = reinterpret_cast<uintptr_t>(leases[i].data), b = reinterpret_cast<uintptr_t>(leases[j].data);
                if (leases[i].size && leases[j].size && (a <= b ? b - a < leases[i].size : a - b < leases[j].size)) return false;
            }
        std::memcpy(key_.data(), request_key.data, 48);
        std::memcpy(key_.data() + 48, attempt_id.data(), 16);
        TxAttemptView attempt;
        attempt.submitted = submitted;
        size_t size = 0;
        if (!encodeTxAttempt({key_.data(), key_.size()}, attempt, attempt_.data(), attempt_.size(), size)) return false;
        mutations_[0] = {13, {key_.data(), key_.size()}, {attempt_.data(), size}, false};
        root_ = root;
        copy_ = copy;
        workspace_ = &workspace;
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 5, {key_.data(), 48}, frame_, capacity_)) return false;
        result_ = IndexedCommitStep::Working;
        return true;
    }
    bool committed(::geocaching::storage::IndexRootView& root) const
    {
        root = {};
        return result_ == IndexedCommitStep::Verified && std::get<SdIndexedCommit>(io_).committed(root);
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (phase_ == Phase::Commit) return result_ = std::get<SdIndexedCommit>(io_).step();
        auto& read = std::get<SdIndexGet>(io_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready)
            return fail(status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexGetStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                   : IndexedCommitStep::Invalid);
        const ::geocaching::ByteView request_key{key_.data(), 48};
        if (phase_ == Phase::Outgoing)
        {
            OutgoingView outgoing;
            if (!decodeOutgoing(request_key, read.value(), outgoing) || !outgoing.continue_intent ||
                (outgoing.state != 0 && outgoing.state != 3)) return fail(IndexedCommitStep::Invalid);
            std::memcpy(task_id_.data(), outgoing.task_id.data, task_id_.size());
            generation_ = outgoing.install_generation;
            outgoing.state = 1;
            size_t size = 0;
            if (!encodeOutgoing(request_key, outgoing, workspace_->outgoing, workspace_->outgoing_capacity, size)) return fail(IndexedCommitStep::Invalid);
            mutations_[1] = {5, request_key, {workspace_->outgoing, size}, false};
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 10, {task_id_.data(), task_id_.size()}, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
            phase_ = Phase::Task;
            return result_;
        }
        if (phase_ == Phase::Task)
        {
            TaskView task;
            OutgoingView outgoing;
            const ::geocaching::ByteView task_key{task_id_.data(), task_id_.size()};
            if (!decodeTask(task_key, read.value(), task) || !task.continue_intent || task.state > 2 ||
                !decodeOutgoing(request_key, mutations_[1].value, outgoing) ||
                !requestBelongsToTask(task_key, task, request_key, outgoing)) return fail(IndexedCommitStep::Invalid);
            const bool check_generation = (task.kind == 2 || task.kind == 4) && generation_;
            if (check_generation)
            {
                if (task.cache_id.size != cache_id_.size()) return fail(IndexedCommitStep::Invalid);
                std::memcpy(cache_id_.data(), task.cache_id.data, cache_id_.size());
            }
            if (task.state == 0) task.state = 1;
            size_t size = 0;
            if (!encodeTask(task_key, task, workspace_->task.data(), workspace_->task.size(), size)) return fail(IndexedCommitStep::Invalid);
            mutations_[2] = {10, task_key, {workspace_->task.data(), size}, false};
            if (!check_generation) return commit();
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 2, {cache_id_.data(), cache_id_.size()}, frame_, capacity_)) return fail(IndexedCommitStep::Invalid);
            phase_ = Phase::Head;
            return result_;
        }
        CacheHeadView head;
        if (!decodeCacheHead({cache_id_.data(), cache_id_.size()}, read.value(), head) || head.install_generation != generation_)
            return fail(IndexedCommitStep::Invalid);
        return commit();
    }

  private:
    enum class Phase : uint8_t
    {
        Outgoing,
        Task,
        Head,
        Commit
    };
    IndexedCommitStep commit()
    {
        if (!io_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, mutations_.data(), mutations_.size(), frame_, capacity_, *candidate_, false, 1))
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
    ::geocaching::storage::QueuedRequestWorkspace* workspace_ = nullptr;
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    std::array<::geocaching::storage::MutationView, 3> mutations_{};
    std::array<uint8_t, 64> key_{};
    std::array<uint8_t, 16> task_id_{};
    std::array<uint8_t, 32> cache_id_{};
    std::array<uint8_t, 192> attempt_{};
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0;
    uint64_t generation_ = 0;
    unsigned copy_ = 0;
    std::variant<std::monostate, SdIndexGet, SdIndexedCommit> io_;
    Phase phase_ = Phase::Outgoing;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedBeginAttempt) <= 1664, "Send intent must not own request payloads");
} // namespace platform::esp::arduino_common::geocaching
