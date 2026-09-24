#pragma once
#include "geocaching/storage/attempt_timeout.h"
#include "platform/esp/arduino_common/geocaching/index_workspace_owner.h"
#include "platform/esp/arduino_common/geocaching/request_dispatch_store.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_attempt_update.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_begin_attempt.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_pending_request.h"

namespace platform::esp::arduino_common::geocaching
{
// The session owns roots and workspaces. Other operations may publish a root
// only while !busy(). Completed mutation roots update the same session view.
class IndexedDispatchStore final : public RequestDispatchStore
{
  public:
    IndexedDispatchStore(const ::geocaching::storage::VolumeInstance& volume,
                         ::geocaching::storage::IndexRootView& root, unsigned& copy,
                         ::geocaching::storage::IndexRootBytes& first, ::geocaching::storage::IndexRootBytes& second,
                         IndexWorkspaceOwner& owner, ::geocaching::storage::QueuedRequestWorkspace& workspace, uint8_t* frame, size_t capacity)
        : volume_(volume), root_(root), copy_(copy), roots_{&first, &second}, owner_(owner), workspace_(workspace), frame_(frame), capacity_(capacity)
    {
        blocked_ = copy > 1 || !::geocaching::storage::validIndexRoot(root) || root.shards.data != roots_[copy]->data() + 48;
    }
    bool needsRecovery() const override { return blocked_; }
    void bindWorkspace(uint8_t* frame) { frame_ = frame; }
    ~IndexedDispatchStore() override { clear(); }
    bool busy() const { return phase_ != Phase::None; }
    bool commitPending() const override { return phase_ == Phase::Begin || phase_ == Phase::Update || phase_ == Phase::Expire; }
    bool expirationChanged() const override { return expired_; }
    DispatchReadResult readPending(const ::geocaching::Destination& local, ::geocaching::ByteView after,
                                   ::geocaching::storage::PendingRequestView& out) override
    {
        out = {};
        if (blocked_) return DispatchReadResult::Corrupt;
        if (phase_ == Phase::None)
        {
            if (!owner_.acquire(this)) return DispatchReadResult::Pending;
            revision_ = root_.revision;
            if (!io_.emplace<SdIndexedPendingRequest>(volume_).begin(root_, local, after, frame_, capacity_)) return readError();
            phase_ = Phase::Select;
            return DispatchReadResult::Pending;
        }
        if (phase_ != Phase::Select || revision_ != root_.revision) return readError();
        auto& select = std::get<SdIndexedPendingRequest>(io_);
        const auto status = select.step();
        if (status == IndexedPendingStep::Working) return DispatchReadResult::Pending;
        if (status == IndexedPendingStep::Ready && select.selected(out))
        {
            clear();
            return DispatchReadResult::Ready;
        }
        if (status == IndexedPendingStep::None)
        {
            clear();
            return DispatchReadResult::None;
        }
        return readError();
    }
    DispatchReadResult readForSend(::geocaching::ByteView key, DispatchSendView& out) override
    {
        using namespace ::geocaching::storage;
        out = {};
        if (blocked_ || !key.data || key.size != 48) return DispatchReadResult::Corrupt;
        if (phase_ == Phase::None)
        {
            if (!owner_.acquire(this)) return DispatchReadResult::Pending;
            revision_ = root_.revision;
            std::memcpy(key_.data(), key.data, 48);
            send_size_ = 0;
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 5, {key_.data(), 48}, frame_, capacity_)) return readError();
            phase_ = Phase::SendRequest;
            return DispatchReadResult::Pending;
        }
        if (revision_ != root_.revision || std::memcmp(key_.data(), key.data, 48) ||
            (phase_ != Phase::SendRequest && phase_ != Phase::SendTask && phase_ != Phase::SendReload)) return readError();
        auto& read = std::get<SdIndexGet>(io_);
        const auto status = read.step();
        if (status == IndexGetStep::Working) return DispatchReadResult::Pending;
        if (status != IndexGetStep::Ready) return readError();
        if (phase_ == Phase::SendTask)
        {
            TaskView task;
            if (!decodeTask({task_id_.data(), task_id_.size()}, read.value(), task)) return readError();
            bool linked = false;
            for (size_t i = 0; i < task.request_count; ++i) linked |= !std::memcmp(task.requests[i].data, key_.data(), 48);
            if (!linked) return readError();
            stopped_ = stopped_ || !task.continue_intent || task.state == 5;
            if (stopped_)
            {
                out.stopped = true;
                clear();
                return DispatchReadResult::Ready;
            }
            if (send_size_)
            {
                out.request = {workspace_.outgoing, send_size_};
                out.stopped = false;
                clear();
                return DispatchReadResult::Ready;
            }
            if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 5, {key_.data(), 48}, frame_, capacity_)) return readError();
            phase_ = Phase::SendReload;
            return DispatchReadResult::Pending;
        }
        OutgoingView outgoing;
        if (!decodeOutgoing({key_.data(), 48}, read.value(), outgoing) || outgoing.state != 1) return readError();
        if (phase_ == Phase::SendReload)
        {
            out.request = outgoing.request;
            out.stopped = stopped_;
            clear();
            return DispatchReadResult::Ready;
        }
        std::memcpy(task_id_.data(), outgoing.task_id.data, task_id_.size());
        stopped_ = !outgoing.continue_intent;
        // The encoding workspace is idle between the durable begin-attempt
        // commit and transport submission. Preserve the request there while
        // the task lookup reuses frame_. Small/aliased workspaces fall back to
        // the original reload path; no extra payload allocation is required.
        const auto disjoint = [&](const uint8_t* bytes, size_t size)
        {
            const auto a = reinterpret_cast<uintptr_t>(workspace_.outgoing), b = reinterpret_cast<uintptr_t>(bytes);
            return a <= b ? b - a >= outgoing.request.size : a - b >= size;
        };
        if (workspace_.outgoing && outgoing.request.size <= workspace_.outgoing_capacity &&
            disjoint(frame_, capacity_) && disjoint(roots_[0]->data(), roots_[0]->size()) &&
            disjoint(roots_[1]->data(), roots_[1]->size()))
        {
            std::memcpy(workspace_.outgoing, outgoing.request.data, outgoing.request.size);
            send_size_ = outgoing.request.size;
        }
        if (!io_.emplace<SdIndexGet>(volume_).begin(root_, 10, {task_id_.data(), task_id_.size()}, frame_, capacity_)) return readError();
        phase_ = Phase::SendTask;
        return DispatchReadResult::Pending;
    }
    JournalWriteResult beginAttempt(const ::geocaching::Destination& local, ::geocaching::ByteView key,
                                    const std::array<uint8_t, 16>& id, const ::geocaching::storage::StoredTime& time) override
    {
        if (blocked_) return JournalWriteResult::Unavailable;
        if (busy()) return JournalWriteResult::Busy;
        if (!owner_.acquire(this)) return JournalWriteResult::Busy;
        revision_ = root_.revision;
        if (!io_.emplace<SdIndexedBeginAttempt>(volume_).begin(root_, copy_, local, key, id, time, workspace_, frame_, capacity_, *roots_[1 - copy_]))
        {
            clear();
            return JournalWriteResult::Invalid;
        }
        phase_ = Phase::Begin;
        return JournalWriteResult::InProgress;
    }
    JournalWriteResult recordAttemptHash(::geocaching::ByteView key, const std::array<uint8_t, 32>& hash) override
    {
        return update(key, {hash.data(), hash.size()}, ::geocaching::storage::TxAttemptState::Failed, {});
    }
    JournalWriteResult finishAttempt(::geocaching::ByteView key, ::geocaching::storage::TxAttemptState terminal,
                                     const ::geocaching::storage::StoredTime& time) override
    {
        return update(key, {}, terminal, time);
    }
    JournalWriteResult expireOneAttempt(const ::geocaching::storage::StoredTime& now, uint64_t started, uint64_t timeout, bool& expired) override
    {
        expired = false;
        if (blocked_) return JournalWriteResult::Unavailable;
        if (busy()) return JournalWriteResult::Busy;
        expired_ = false;
        if (!timeout) return JournalWriteResult::Verified;
        if (!owner_.acquire(this)) return JournalWriteResult::Busy;
        revision_ = root_.revision;
        now_ = now;
        started_ = started;
        timeout_ = timeout;
        if (!io_.emplace<SdIndexScan>(volume_).begin(root_, 13, frame_, capacity_))
        {
            clear();
            return JournalWriteResult::Invalid;
        }
        phase_ = Phase::Expire;
        return JournalWriteResult::InProgress;
    }
    JournalWriteResult stepCommit() override
    {
        using namespace ::geocaching::storage;
        if (blocked_) return JournalWriteResult::Unavailable;
        if (!commitPending()) return JournalWriteResult::Invalid;
        if (!owner_.heldBy(this) || root_.revision != revision_)
        {
            blocked_ = true;
            clear();
            return JournalWriteResult::StateRejected;
        }
        if (phase_ == Phase::Expire)
        {
            auto& scan = std::get<SdIndexScan>(io_);
            const auto status = scan.step();
            if (status == IndexScanStep::Working) return JournalWriteResult::InProgress;
            if (status == IndexScanStep::End)
            {
                clear();
                return JournalWriteResult::Verified;
            }
            MutationView row;
            TxAttemptView attempt;
            if (status != IndexScanStep::Item || !scan.item(row) || !decodeTxAttempt(row.key, row.value, attempt))
            {
                blocked_ = true;
                clear();
                return JournalWriteResult::StateRejected;
            }
            if (!attemptTimeoutReached(attempt, now_, started_, timeout_))
            {
                if (!scan.advance())
                {
                    blocked_ = true;
                    clear();
                    return JournalWriteResult::StateRejected;
                }
                return JournalWriteResult::InProgress;
            }
            std::memcpy(key_.data(), row.key.data, key_.size());
            clear();
            const auto begun = update({key_.data(), key_.size()}, {}, TxAttemptState::Failed, now_);
            expired_ = begun == JournalWriteResult::InProgress;
            return begun;
        }
        const auto status = phase_ == Phase::Begin ? std::get<SdIndexedBeginAttempt>(io_).step() : std::get<SdIndexedAttemptUpdate>(io_).step();
        if (status == IndexedCommitStep::Working) return JournalWriteResult::InProgress;
        if (status == IndexedCommitStep::Verified)
        {
            IndexRootView committed;
            const bool ready = phase_ == Phase::Begin ? std::get<SdIndexedBeginAttempt>(io_).committed(committed) : std::get<SdIndexedAttemptUpdate>(io_).committed(committed);
            if (!ready)
            {
                blocked_ = true;
                clear();
                return JournalWriteResult::StateRejected;
            }
            if (committed.revision != root_.revision) copy_ = 1 - copy_;
            root_ = committed;
            clear();
            return JournalWriteResult::Verified;
        }
        blocked_ = status == IndexedCommitStep::IoError || status == IndexedCommitStep::VolumeChanged || status == IndexedCommitStep::RecoveryRequired;
        clear();
        return status == IndexedCommitStep::VolumeChanged ? JournalWriteResult::VolumeChanged : status == IndexedCommitStep::IoError ? JournalWriteResult::IoError
                                                                                                                                     : JournalWriteResult::StateRejected;
    }

  private:
    enum class Phase : uint8_t
    {
        None,
        Select,
        SendRequest,
        SendTask,
        SendReload,
        Begin,
        Update,
        Expire
    };
    JournalWriteResult update(::geocaching::ByteView key, ::geocaching::ByteView hash, ::geocaching::storage::TxAttemptState terminal,
                              const ::geocaching::storage::StoredTime& time)
    {
        if (blocked_) return JournalWriteResult::Unavailable;
        if (busy()) return JournalWriteResult::Busy;
        if (!owner_.acquire(this)) return JournalWriteResult::Busy;
        revision_ = root_.revision;
        if (!io_.emplace<SdIndexedAttemptUpdate>(volume_).begin(root_, copy_, key, hash, terminal, time,
                                                                workspace_.outgoing, workspace_.outgoing_capacity, frame_, capacity_, *roots_[1 - copy_]))
        {
            clear();
            return JournalWriteResult::Invalid;
        }
        phase_ = Phase::Update;
        return JournalWriteResult::InProgress;
    }
    void clear()
    {
        io_.emplace<std::monostate>();
        phase_ = Phase::None;
        owner_.release(this);
    }
    DispatchReadResult readError()
    {
        blocked_ = true;
        clear();
        return DispatchReadResult::Corrupt;
    }
    ::geocaching::storage::VolumeInstance volume_;
    ::geocaching::storage::IndexRootView& root_;
    unsigned& copy_;
    ::geocaching::storage::IndexRootBytes* roots_[2];
    IndexWorkspaceOwner& owner_;
    ::geocaching::storage::QueuedRequestWorkspace& workspace_;
    uint8_t* frame_;
    size_t capacity_, send_size_ = 0;
    ::geocaching::storage::StoredTime now_;
    std::array<uint8_t, 64> key_{};
    std::array<uint8_t, 16> task_id_{};
    uint64_t revision_ = 0, started_ = 0, timeout_ = 0;
    bool blocked_ = false, stopped_ = false, expired_ = false;
    std::variant<std::monostate, SdIndexedPendingRequest, SdIndexGet, SdIndexScan, SdIndexedBeginAttempt, SdIndexedAttemptUpdate> io_;
    Phase phase_ = Phase::None;
};
static_assert(sizeof(IndexedDispatchStore) <= 2048, "Dispatch store overlays bounded operations, not a logical ledger");
} // namespace platform::esp::arduino_common::geocaching
