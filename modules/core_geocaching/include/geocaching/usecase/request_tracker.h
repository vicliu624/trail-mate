#pragma once
#include "geocaching/domain/record.h"

namespace geocaching
{
enum class RequestPhase : std::uint8_t
{
    Idle,
    Queued,
    InFlight,
    AwaitingResult,
    Unconfirmed,
    ResultRecorded,
};

enum class ResultAdmission : std::uint8_t
{
    Accepted,
    Duplicate,
    WrongRequest,
    ConflictingResult,
};

// One logical request. Ownership/persistence are external: call recordResult
// only after the authenticated, fully validated result has durably committed.
class RequestTracker
{
  public:
    bool begin(const Destination& local, const Destination& remote,
               const RequestId& id, Operation operation, std::uint16_t budget)
    {
        if (phase_ != RequestPhase::Idle || budget < 512 || budget > kMaxApplicationBytes ||
            static_cast<std::uint8_t>(operation) > 4) return false;
        local_ = local;
        remote_ = remote;
        id_ = id;
        operation_ = operation;
        budget_ = budget;
        continue_ = true;
        phase_ = RequestPhase::Queued;
        return true;
    }

    bool startAttempt()
    {
        if (!continue_ || (phase_ != RequestPhase::Queued && phase_ != RequestPhase::Unconfirmed)) return false;
        phase_ = RequestPhase::InFlight;
        return true;
    }

    void delivered()
    {
        if (phase_ == RequestPhase::InFlight) phase_ = RequestPhase::AwaitingResult;
    }

    void unconfirmed()
    {
        if (phase_ == RequestPhase::InFlight || phase_ == RequestPhase::AwaitingResult)
            phase_ = RequestPhase::Unconfirmed;
    }

    void stopFurtherAttempts() { continue_ = false; }

    ResultAdmission classifyResult(const Destination& source, const Destination& recipient,
                                   const RequestId& id, Operation operation,
                                   std::size_t payload_size,
                                   const std::array<std::uint8_t, 32>& result_digest) const
    {
        if (phase_ == RequestPhase::Idle || source.bytes != remote_.bytes ||
            recipient.bytes != local_.bytes || id.bytes != id_.bytes ||
            operation != operation_ || payload_size == 0 || payload_size > budget_)
            return ResultAdmission::WrongRequest;
        if (phase_ == RequestPhase::ResultRecorded)
            return result_digest == result_digest_ ? ResultAdmission::Duplicate : ResultAdmission::ConflictingResult;
        return ResultAdmission::Accepted;
    }

    bool recordResult(const Destination& source, const Destination& recipient,
                      const RequestId& id, Operation operation, std::size_t payload_size,
                      const std::array<std::uint8_t, 32>& result_digest)
    {
        if (classifyResult(source, recipient, id, operation, payload_size, result_digest) != ResultAdmission::Accepted)
            return false;
        result_digest_ = result_digest;
        phase_ = RequestPhase::ResultRecorded;
        return true;
    }

    RequestPhase phase() const { return phase_; }
    bool continueRequested() const { return continue_; }

  private:
    Destination local_;
    Destination remote_;
    RequestId id_;
    Operation operation_ = Operation::Capabilities;
    std::uint16_t budget_ = 0;
    RequestPhase phase_ = RequestPhase::Idle;
    bool continue_ = false;
    std::array<std::uint8_t, 32> result_digest_{};
};
} // namespace geocaching
