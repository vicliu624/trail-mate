#pragma once
#include "geocaching/usecase/publish_attempt.h"
#include "platform/esp/arduino_common/geocaching/sd_request_store.h"

namespace platform::esp::arduino_common::geocaching
{
class SdPublishPort final : public ::geocaching::PublishAttemptPort
{
  public:
    using Result = ::geocaching::PublishPersistence;
    SdPublishPort(SdRequestStore& store, ::geocaching::protocol::RecordCrypto& crypto,
                  const ::geocaching::Destination& local, const ::geocaching::GeocacheId& cache,
                  const ::geocaching::RevisionHash& hash, const std::array<uint8_t, 16>& task,
                  const ::geocaching::storage::StoredTime& created)
        : store_(store), crypto_(crypto), local_(local), cache_(cache), hash_(hash), task_(task), created_(created) {}

    Result submit(const ::geocaching::Destination& remote, const ::geocaching::RequestId& request, ::geocaching::ByteView bytes) override
    {
        if (phase_ != Phase::Idle) return Result::Rejected;
        const ::geocaching::storage::RequestTaskTarget target{{cache_.bytes.data(), 32}, {hash_.bytes.data(), 32}, 0};
        const auto result = store_.persistNewTask(local_, remote, request, task_, 1, bytes, created_, target);
        if (result != JournalWriteResult::InProgress && result != JournalWriteResult::Verified) return Result::Rejected;
        remote_ = remote;
        request_ = request;
        phase_ = result == JournalWriteResult::InProgress ? Phase::Submitting : Phase::Waiting;
        return convert(result);
    }
    // Called after owner validates the persisted Task/Outgoing and original ID.
    bool attachRestoredRequest(const ::geocaching::Destination& remote, const ::geocaching::RequestId& request)
    {
        if (phase_ != Phase::Idle) return false;
        remote_ = remote;
        request_ = request;
        phase_ = Phase::Waiting;
        return true;
    }
    Result commitResult(const ::geocaching::Destination& remote, const ::geocaching::RequestId& request, ::geocaching::ByteView bytes) override
    {
        if (phase_ != Phase::Waiting || !matches(remote, request)) return Result::Rejected;
        const auto result = store_.commitPublishResult(local_, remote, request, bytes, crypto_);
        if (result == JournalWriteResult::InProgress) phase_ = Phase::Committing;
        else if (result == JournalWriteResult::Verified) phase_ = Phase::Complete;
        return convert(result);
    }
    Result cancel(const ::geocaching::Destination& remote, const ::geocaching::RequestId& request) override
    {
        if (!matches(remote, request) || (phase_ != Phase::Submitting && phase_ != Phase::Waiting)) return Result::Rejected;
        if (phase_ == Phase::Submitting)
        {
            stop_after_submit_ = true;
            return Result::Pending;
        }
        return stop();
    }
    Result poll() override
    {
        if (phase_ != Phase::Submitting && phase_ != Phase::Committing && phase_ != Phase::Stopping) return Result::Rejected;
        const auto result = store_.stepCommit();
        if (result == JournalWriteResult::InProgress) return Result::Pending;
        if (result != JournalWriteResult::Verified)
        {
            phase_ = Phase::Failed;
            return Result::Rejected;
        }
        const bool submitted = phase_ == Phase::Submitting;
        phase_ = submitted ? Phase::Waiting : Phase::Complete;
        if (submitted && stop_after_submit_) return stop();
        return Result::Committed;
    }

  private:
    enum class Phase : uint8_t
    {
        Idle,
        Submitting,
        Waiting,
        Committing,
        Stopping,
        Complete,
        Failed
    };
    static Result convert(JournalWriteResult value)
    {
        return value == JournalWriteResult::InProgress ? Result::Pending : value == JournalWriteResult::Verified ? Result::Committed
                                                                                                                 : Result::Rejected;
    }
    bool matches(const ::geocaching::Destination& remote, const ::geocaching::RequestId& request) const
    {
        return remote.bytes == remote_.bytes && request.bytes == request_.bytes;
    }
    Result stop()
    {
        const auto result = store_.stopTask(task_);
        phase_ = result == JournalWriteResult::InProgress ? Phase::Stopping : result == JournalWriteResult::Verified ? Phase::Complete
                                                                                                                     : Phase::Failed;
        return convert(result);
    }
    SdRequestStore& store_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::Destination local_, remote_;
    ::geocaching::RequestId request_;
    ::geocaching::GeocacheId cache_;
    ::geocaching::RevisionHash hash_;
    std::array<uint8_t, 16> task_;
    ::geocaching::storage::StoredTime created_;
    Phase phase_ = Phase::Idle;
    bool stop_after_submit_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
