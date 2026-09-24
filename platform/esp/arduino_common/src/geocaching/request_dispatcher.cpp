#include "platform/esp/arduino_common/geocaching/request_dispatcher.h"
#include "esp_random.h"

namespace platform::esp::arduino_common::geocaching
{
RequestDispatcher::RequestDispatcher(chat::MeshAdapterRouter& router, RequestDispatchStore& store,
                                     uint32_t retry_delay_ms, uint64_t attempt_timeout_ms)
    : router_(router), store_(store), retry_delay_ms_(retry_delay_ms), attempt_timeout_ms_(attempt_timeout_ms) {}

DispatchResult RequestDispatcher::dispatchOne(const ::geocaching::storage::StoredTime& now, ::geocaching::ByteView preferred)
{
    using namespace ::geocaching::storage;
    if (store_.needsRecovery()) return {DispatchStatus::StorageBlocked};
    const auto defer = [&]()
    {
        not_before_ = now.monotonic_ms > UINT64_MAX - retry_delay_ms_ ? UINT64_MAX : now.monotonic_ms + retry_delay_ms_;
    };
    const auto begin = [&](const ::geocaching::Destination& local) -> DispatchResult
    {
        esp_fill_random(attempt_id_.data(), attempt_id_.size());
        const auto begun = store_.beginAttempt(local, {cursor_.data(), cursor_.size()}, attempt_id_, now);
        if (begun == JournalWriteResult::InProgress)
        {
            phase_ = Phase::BeginCommit;
            return {DispatchStatus::Deferred};
        }
        if (begun == JournalWriteResult::Verified)
        {
            phase_ = Phase::Send;
            return {DispatchStatus::Deferred};
        }
        return {begun == JournalWriteResult::StateRejected || begun == JournalWriteResult::Busy
                    ? DispatchStatus::Deferred
                    : DispatchStatus::StorageBlocked};
    };
    if (phase_ != Phase::Select && phase_ != Phase::SelectRequest && phase_ != Phase::Send)
    {
        const auto result = store_.stepCommit();
        if (result == JournalWriteResult::InProgress) return {DispatchStatus::Deferred};
        const auto completed = phase_;
        phase_ = Phase::Select;
        if (result != JournalWriteResult::Verified) return {DispatchStatus::StorageBlocked};
        if (completed == Phase::BeginCommit)
        {
            phase_ = Phase::Send;
            return {DispatchStatus::Deferred};
        }
        if (completed == Phase::SuccessCommit) return {DispatchStatus::Submitted};
        if (completed == Phase::ExpireCommit && !store_.expirationChanged())
        {
            phase_ = Phase::SelectRequest;
            return {DispatchStatus::Deferred};
        }
        defer();
        return {DispatchStatus::Deferred, completed == Phase::FailureCommit ? send_failure_ : chat::MeshOperationFailure::None};
    }
    // Another serialized operation may own the pending transaction.
    if (store_.commitPending()) return {DispatchStatus::Deferred};
    if (phase_ == Phase::Send)
    {
        DispatchSendView outgoing;
        const auto read = store_.readForSend({cursor_.data(), cursor_.size()}, outgoing);
        if (read == DispatchReadResult::Pending) return {DispatchStatus::Deferred};
        if (read != DispatchReadResult::Ready)
        {
            phase_ = Phase::Select;
            return {DispatchStatus::Corrupt};
        }
        std::array<uint8_t, 64> attempt_key{};
        std::memcpy(attempt_key.data(), cursor_.data(), 48);
        std::memcpy(attempt_key.data() + 48, attempt_id_.data(), 16);
        JournalWriteResult saved;
        if (outgoing.stopped)
        {
            send_failure_ = chat::MeshOperationFailure::None;
            saved = store_.finishAttempt({attempt_key.data(), attempt_key.size()}, TxAttemptState::CancelledBeforeSend, now);
            phase_ = Phase::FailureCommit;
        }
        else
        {
            std::array<uint8_t, 32> hash{};
            // Source/destination come from the committed request key. The
            // router checks the current local identity while holding its lock.
            const auto sent = router_.sendGeocachingData(cursor_.data() + 16,
                                                         {outgoing.request.data, outgoing.request.size}, false, &hash, cursor_.data());
            send_failure_ = sent.failure;
            saved = sent.ok ? store_.recordAttemptHash({attempt_key.data(), attempt_key.size()}, hash)
                            : store_.finishAttempt({attempt_key.data(), attempt_key.size()}, TxAttemptState::Failed, now);
            phase_ = sent.ok ? Phase::SuccessCommit : Phase::FailureCommit;
        }
        if (saved == JournalWriteResult::InProgress) return {DispatchStatus::Deferred};
        const bool success = phase_ == Phase::SuccessCommit;
        phase_ = Phase::Select;
        if (saved != JournalWriteResult::Verified) return {DispatchStatus::StorageBlocked};
        if (!success) defer();
        return {success ? DispatchStatus::Submitted : DispatchStatus::Deferred, send_failure_};
    }
    if (!clock_initialized_ || boot_ != now.boot_id)
    {
        boot_ = now.boot_id;
        not_before_ = 0;
        has_cursor_ = false;
        has_preferred_ = false;
        recovery_started_ms_ = now.monotonic_ms;
        clock_initialized_ = true;
    }
    if (phase_ == Phase::Select)
    {
        // A background scan may already have selected the same request before
        // the caller supplied its hint. Do not reserve it a second time.
        if (preferred.data && preferred.size == preferred_.size() && has_cursor_ &&
            !std::memcmp(preferred.data, cursor_.data(), cursor_.size()))
        {
            preferred_ = cursor_;
            has_preferred_ = true;
        }
        if (preferred.data && preferred.size == preferred_.size() &&
            (!has_preferred_ || std::memcmp(preferred.data, preferred_.data(), preferred_.size())))
        {
            ::geocaching::Destination local;
            if (!router_.getGeocachingDispatchDestination(local.bytes.data()) || std::memcmp(preferred.data, local.bytes.data(), 16))
                return {DispatchStatus::Deferred, chat::MeshOperationFailure::NotReady};
            std::memcpy(cursor_.data(), preferred.data, cursor_.size());
            has_cursor_ = true;
            const auto result = begin(local);
            if (phase_ == Phase::BeginCommit || phase_ == Phase::Send)
            {
                preferred_ = cursor_;
                has_preferred_ = true;
            }
            return result;
        }
        bool expired = false;
        const auto expiration = store_.expireOneAttempt(now, recovery_started_ms_, attempt_timeout_ms_, expired);
        if (expiration == JournalWriteResult::Busy) return {DispatchStatus::Deferred};
        if (expiration == JournalWriteResult::InProgress)
        {
            phase_ = Phase::ExpireCommit;
            return {DispatchStatus::Deferred};
        }
        if (expiration != JournalWriteResult::Verified) return {DispatchStatus::StorageBlocked};
        if (expired)
        {
            defer();
            return {DispatchStatus::Deferred};
        }
        if (now.monotonic_ms < not_before_) return {DispatchStatus::Deferred};
        phase_ = Phase::SelectRequest;
    }
    ::geocaching::Destination local;
    if (now.monotonic_ms < not_before_) return {DispatchStatus::Deferred};
    if (!router_.getGeocachingDispatchDestination(local.bytes.data()))
        return {DispatchStatus::Deferred, chat::MeshOperationFailure::NotReady};
    PendingRequestView pending;
    auto selected = store_.readPending(local, has_cursor_ ? ::geocaching::ByteView{cursor_.data(), cursor_.size()} : ::geocaching::ByteView{}, pending);
    if (selected == DispatchReadResult::Pending) return {DispatchStatus::Deferred};
    if (selected == DispatchReadResult::None && has_cursor_)
    {
        has_cursor_ = false;
        return {DispatchStatus::Deferred};
    }
    phase_ = Phase::Select;
    if (selected == DispatchReadResult::Corrupt) return {DispatchStatus::Corrupt};
    if (selected == DispatchReadResult::None) return {DispatchStatus::Idle};
    cursor_ = pending.key;
    has_cursor_ = true;
    return begin(local);
}
} // namespace platform::esp::arduino_common::geocaching
