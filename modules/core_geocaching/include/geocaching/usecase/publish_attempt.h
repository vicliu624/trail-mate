#pragma once
#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/publish_response.h"
#include "geocaching/protocol/verify_record.h"

namespace geocaching
{
enum class PublishPersistence : uint8_t
{
    Rejected,
    Pending,
    Committed
};
class PublishAttemptPort
{
  public:
    virtual ~PublishAttemptPort() = default;
    // Persist exact bytes and task linkage before admitting transport work.
    // Input is borrowed for this call only; Pending owns a stable copy.
    virtual PublishPersistence submit(const Destination&, const RequestId&, ByteView request) = 0;
    virtual PublishPersistence commitResult(const Destination&, const RequestId&, ByteView response) = 0;
    virtual PublishPersistence cancel(const Destination&, const RequestId&) = 0;
    virtual PublishPersistence poll() = 0;
};
enum class PublishAttemptPhase : uint8_t
{
    Idle,
    Waiting,
    Confirmed,
    Cancelled,
    Submitting,
    Committing,
    Cancelling,
    Failed
};

// One destination of a public publication task; not the multi-directory quorum
// itself. A caller-owned transient lease is reused for verification and request
// encoding, and may be released when begin returns. Transport/source checks
// happen before accept(), and request/body policies remain owner responsibilities.
class PublishAttempt
{
  public:
    PublishAttempt(PublishAttemptPort& port, protocol::RecordCrypto& crypto) : port_(port), crypto_(crypto) {}
    // Owner supplies an already durable request/result and attaches its port to
    // that same task. No submit, new request ID, or persistence occurs here.
    bool resume(const Destination& destination, const RequestId& request, ByteView encoded_request,
                uint8_t* workspace, size_t capacity, const GeocacheId& expected_id, const RevisionHash& expected_hash,
                ByteView durable_response = {})
    {
        if (phase_ != PublishAttemptPhase::Idle || !workspace) return false;
        const auto overlaps = [&](ByteView bytes)
        {
            if (!bytes.size) return false;
            if (!bytes.data) return true;
            const auto input = reinterpret_cast<uintptr_t>(bytes.data), output = reinterpret_cast<uintptr_t>(workspace);
            return input <= output ? output - input < bytes.size : input - output < capacity;
        };
        if (overlaps(encoded_request) || overlaps(durable_response)) return false;
        protocol::PublishRequestView parsed;
        protocol::VerifiedRecordView verified;
        if (!protocol::decodePublishRequest(encoded_request, request, parsed) ||
            protocol::verifyGeocache(parsed.signed_cache, crypto_, workspace, capacity, verified, &expected_id, &expected_hash) !=
                protocol::VerificationResult::Valid) return false;
        protocol::PublishDisposition disposition;
        if (durable_response.size && !protocol::decodePublishResponse(durable_response, request, verified.id, verified.hash,
                                                                      verified.record.revision, verified.record.state, disposition)) return false;
        destination_ = destination;
        request_ = request;
        id_ = verified.id;
        hash_ = verified.hash;
        revision_ = verified.record.revision;
        state_ = verified.record.state;
        if (durable_response.size) disposition_ = disposition;
        phase_ = durable_response.size ? PublishAttemptPhase::Confirmed : PublishAttemptPhase::Waiting;
        return true;
    }
    bool begin(const Destination& destination, const RequestId& request, ByteView signed_record,
               uint8_t* workspace, size_t capacity)
    {
        // At a 512-byte response budget, the request envelope adds 26 bytes
        // to the original SignedCache. This also covers verification scratch.
        if (phase_ != PublishAttemptPhase::Idle || !signed_record.data || signed_record.size > 4166 ||
            !workspace || capacity < signed_record.size + 26) return false;
        const auto input = reinterpret_cast<uintptr_t>(signed_record.data);
        const auto scratch = reinterpret_cast<uintptr_t>(workspace);
        if (input <= scratch ? scratch - input < signed_record.size : input - scratch < capacity) return false;
        protocol::VerifiedRecordView verified;
        if (protocol::verifyGeocache(signed_record, crypto_, workspace, capacity, verified) != protocol::VerificationResult::Valid)
            return false;
        size_t size = 0;
        if (!protocol::encodePublishRequest(request, verified.record.encoded, verified.signature, 512,
                                            workspace, capacity, size)) return false;
        // Capture borrowed input metadata before a durable submit can swap
        // the storage arena that supplied the signed record.
        destination_ = destination;
        request_ = request;
        id_ = verified.id;
        hash_ = verified.hash;
        revision_ = verified.record.revision;
        state_ = verified.record.state;
        const auto result = port_.submit(destination, request, {workspace, size});
        if (result == PublishPersistence::Rejected) return false;
        phase_ = result == PublishPersistence::Pending ? PublishAttemptPhase::Submitting : PublishAttemptPhase::Waiting;
        return true;
    }
    bool accept(const Destination& source, ByteView response)
    {
        if (source.bytes != destination_.bytes || !response.data) return false;
        if (phase_ != PublishAttemptPhase::Waiting && phase_ != PublishAttemptPhase::Confirmed) return false;
        protocol::PublishDisposition disposition;
        if (!protocol::decodePublishResponse(response, request_, id_, hash_, revision_, state_, disposition)) return false;
        // CMP1 is canonical and every other response field was compared above.
        // Retaining the disposition is sufficient to reject a different result
        // without retaining another 512-byte encoded response.
        if (phase_ == PublishAttemptPhase::Confirmed) return disposition == disposition_;
        const auto result = port_.commitResult(source, request_, response);
        if (result == PublishPersistence::Rejected) return false;
        disposition_ = disposition;
        phase_ = result == PublishPersistence::Pending ? PublishAttemptPhase::Committing : PublishAttemptPhase::Confirmed;
        return result == PublishPersistence::Committed;
    }
    void advance()
    {
        if (phase_ != PublishAttemptPhase::Submitting && phase_ != PublishAttemptPhase::Committing &&
            phase_ != PublishAttemptPhase::Cancelling) return;
        const auto result = port_.poll();
        if (result == PublishPersistence::Pending) return;
        if (result == PublishPersistence::Rejected)
        {
            phase_ = PublishAttemptPhase::Failed;
            return;
        }
        phase_ = phase_ == PublishAttemptPhase::Submitting ? PublishAttemptPhase::Waiting : phase_ == PublishAttemptPhase::Committing ? PublishAttemptPhase::Confirmed
                                                                                                                                      : PublishAttemptPhase::Cancelled;
    }
    bool cancel()
    {
        // Once an acknowledgement is being committed, preserve its result.
        if (phase_ == PublishAttemptPhase::Confirmed || phase_ == PublishAttemptPhase::Committing) return false;
        if (phase_ == PublishAttemptPhase::Cancelled || phase_ == PublishAttemptPhase::Cancelling) return true;
        if (phase_ == PublishAttemptPhase::Idle)
        {
            phase_ = PublishAttemptPhase::Cancelled;
            return true;
        }
        const auto result = port_.cancel(destination_, request_);
        if (result == PublishPersistence::Rejected) return false;
        phase_ = result == PublishPersistence::Pending ? PublishAttemptPhase::Cancelling : PublishAttemptPhase::Cancelled;
        return true;
    }
    PublishAttemptPhase phase() const { return phase_; }

  private:
    PublishAttemptPort& port_;
    protocol::RecordCrypto& crypto_;
    Destination destination_;
    RequestId request_;
    GeocacheId id_;
    RevisionHash hash_;
    uint32_t revision_ = 0;
    CacheState state_ = CacheState::Active;
    protocol::PublishDisposition disposition_ = protocol::PublishDisposition::Stored;
    PublishAttemptPhase phase_ = PublishAttemptPhase::Idle;
};
static_assert(sizeof(PublishAttempt) <= 192, "Publication attempts must not retain payload-sized buffers");
} // namespace geocaching
