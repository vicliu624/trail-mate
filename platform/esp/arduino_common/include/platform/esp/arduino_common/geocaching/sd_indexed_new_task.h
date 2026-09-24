#pragma once
#include "geocaching/protocol/get_request.h"
#include "geocaching/storage/object_ref.h"
#include "geocaching/storage/queued_request.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"

namespace platform::esp::arduino_common::geocaching
{
// The caller leases encoding workspace, frame and roots until inputConsumed /
// completion. The same task/request encoders serve both storage implementations.
class SdIndexedNewTask
{
  public:
    explicit SdIndexedNewTask(const ::geocaching::storage::VolumeInstance& volume) : volume_(volume) {}
    bool begin(const ::geocaching::storage::IndexRootView& root, unsigned copy,
               const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
               const ::geocaching::RequestId& request_id, const std::array<uint8_t, 16>& task_id, uint8_t kind,
               ::geocaching::ByteView request, const ::geocaching::storage::StoredTime& time,
               const ::geocaching::storage::RequestTaskTarget& target,
               ::geocaching::storage::QueuedRequestWorkspace& workspace,
               uint8_t* frame, size_t capacity, ::geocaching::storage::IndexRootBytes& candidate)
    {
        using namespace ::geocaching;
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Idle || copy > 1 || !validIndexRoot(root)) return false;
        const auto overlaps = [](ByteView a, ByteView b)
        {
            const auto x = reinterpret_cast<uintptr_t>(a.data), y = reinterpret_cast<uintptr_t>(b.data);
            return a.size && b.size && (x <= y ? y - x < a.size : x - y < b.size);
        };
        const ByteView outgoing{workspace.outgoing, workspace.outgoing_capacity}, task{workspace.task.data(), workspace.task.size()};
        const ByteView scratch{frame, capacity}, output{candidate.data(), candidate.size()};
        if (overlaps(request, outgoing) || overlaps(request, task) || overlaps(outgoing, task) ||
            overlaps(outgoing, scratch) || overlaps(task, scratch) || overlaps(outgoing, output) || overlaps(task, output) ||
            overlaps(scratch, output) || overlaps(root.shards, output) || overlaps(root.shards, outgoing) || overlaps(root.shards, task) ||
            overlaps(target.cache_id, outgoing) || overlaps(target.cache_id, task) ||
            overlaps(target.revision_hash, outgoing) || overlaps(target.revision_hash, task)) return false;
        task_id_ = task_id;
        // prepareNewRequestTask requires a two-element array reference.
        MutationView prepared[2];
        if (!prepareNewRequestTask(local, remote, request_id, task_id_, kind, request, time, workspace, key_, prepared, target)) return false;
        mutations_[0] = prepared[0];
        mutations_[1] = prepared[1];
        root_ = root;
        copy_ = copy;
        frame_ = frame;
        capacity_ = capacity;
        candidate_ = &candidate;
        count_ = 2;
        result_ = IndexedCommitStep::Working;
        if (kind != 2) return startCommit() == IndexedCommitStep::Working;
        protocol::GetRequestView get;
        if (!protocol::decodeGetRequest(request, request_id, get) || get.wanted_hash.size != 32 ||
            std::memcmp(get.cache_id.data, target.cache_id.data, 32) || std::memcmp(get.wanted_hash.data, target.revision_hash.data, 32))
        {
            fail(IndexedCommitStep::Invalid);
            return false;
        }
        std::memcpy(cache_id_.data(), target.cache_id.data, cache_id_.size());
        generation_ = target.install_generation;
        if (!operation_.emplace<SdIndexGet>(volume_).begin(root_, 2, {cache_id_.data(), cache_id_.size()}, frame_, capacity_))
        {
            fail(IndexedCommitStep::Invalid);
            return false;
        }
        return true;
    }
    bool inputConsumed() const
    {
        return result_ != IndexedCommitStep::Working || (committing_ && std::get<SdIndexedCommit>(operation_).inputConsumed());
    }
    bool committed(::geocaching::storage::IndexRootView& out) const
    {
        out = {};
        return result_ == IndexedCommitStep::Verified && std::get<SdIndexedCommit>(operation_).committed(out);
    }
    IndexedCommitStep step()
    {
        using namespace ::geocaching::storage;
        if (result_ != IndexedCommitStep::Working) return result_;
        if (committing_) return result_ = std::get<SdIndexedCommit>(operation_).step();
        auto& read = std::get<SdIndexGet>(operation_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return result_;
        if (status != IndexGetStep::Ready && status != IndexGetStep::NotFound)
            return fail(status == IndexGetStep::VolumeChanged ? IndexedCommitStep::VolumeChanged : status == IndexGetStep::IoError ? IndexedCommitStep::IoError
                                                                                                                                   : IndexedCommitStep::Invalid);
        CacheHeadView head;
        const ::geocaching::ByteView key{cache_id_.data(), cache_id_.size()};
        if (status == IndexGetStep::Ready && !decodeCacheHead(key, read.value(), head)) return fail(IndexedCommitStep::Invalid);
        if (head.install_generation == UINT64_MAX || generation_ != head.install_generation + 1) return fail(IndexedCommitStep::Invalid);
        head.install_generation = generation_;
        size_t size = 0;
        if (!encodeCacheHead(key, head, head_.data(), head_.size(), size)) return fail(IndexedCommitStep::Invalid);
        mutations_[2] = {2, key, {head_.data(), size}, false};
        count_ = 3;
        return startCommit();
    }

  private:
    IndexedCommitStep startCommit()
    {
        if (!operation_.emplace<SdIndexedCommit>(volume_).begin(root_, copy_, mutations_.data(), count_, frame_, capacity_, *candidate_, false, 2))
            return fail(IndexedCommitStep::Invalid);
        committing_ = true;
        return result_;
    }
    IndexedCommitStep fail(IndexedCommitStep status)
    {
        operation_.emplace<std::monostate>();
        return result_ = status;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView root_;
    std::array<::geocaching::storage::MutationView, 3> mutations_{};
    std::array<uint8_t, 48> key_{};
    std::array<uint8_t, 16> task_id_{};
    std::array<uint8_t, 32> cache_id_{};
    std::array<uint8_t, 64> head_{};
    ::geocaching::storage::IndexRootBytes* candidate_ = nullptr;
    uint8_t* frame_ = nullptr;
    size_t capacity_ = 0, count_ = 0;
    uint64_t generation_ = 0;
    unsigned copy_ = 0;
    bool committing_ = false;
    std::variant<std::monostate, SdIndexGet, SdIndexedCommit> operation_;
    IndexedCommitStep result_ = IndexedCommitStep::Idle;
};
static_assert(sizeof(SdIndexedNewTask) <= 1536, "Task creation owns metadata only");
} // namespace platform::esp::arduino_common::geocaching
