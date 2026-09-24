#pragma once
#include "geocaching/protocol/cmp_writer.h"
#include "geocaching/protocol/get_response.h"
#include "geocaching/protocol/query_response.h"
#include "geocaching/protocol/verify_record.h"

namespace geocaching
{
enum class DownloadOperationResult : uint8_t
{
    Rejected,
    Pending,
    Complete
};
class DownloadPort
{
  public:
    virtual ~DownloadPort() = default;
    virtual DownloadOperationResult submit(const Destination&, const RequestId&, ByteView request) = 0;
    // Atomically install verified GPX and the response result, checking the
    // caller's installation generation. False must never be shown as downloaded.
    // Pending takes responsibility for stable input ownership until completion.
    // Views are borrowed only during the call; stage into owner-held storage.
    virtual DownloadOperationResult commit(const Destination&, const RequestId&, std::uint64_t generation,
                                           ByteView response, const protocol::VerifiedRecordView&) = 0;
    virtual DownloadOperationResult poll() = 0;
    virtual DownloadOperationResult cancel(const Destination&, const RequestId&, uint64_t generation) = 0;
};
enum class DownloadPhase : std::uint8_t
{
    Idle,
    Waiting,
    Stored,
    Cancelled,
    Submitting,
    Installing,
    Cancelling,
    Failed
};

class DownloadClient
{
  public:
    DownloadClient(DownloadPort& port, protocol::RecordCrypto& crypto) : port_(port), crypto_(crypto) {}
    // Owner has restored the matching durable request and started preparing its
    // port. Pending preparation is polled before accepting network responses;
    // it never submits a second task. Copy the borrowed preview immediately.
    bool resume(const Destination& source, const RequestId& request,
                const protocol::SummaryView& summary, std::uint64_t generation,
                DownloadOperationResult prepared = DownloadOperationResult::Complete)
    {
        if (prepared == DownloadOperationResult::Rejected || phase_ != DownloadPhase::Idle || generation == 0 || summary.name.size() > kMaxNameBytes ||
            !protocol::validRecordText(summary.name, false, true)) return false;
        expected_ = summary;
        std::memcpy(name_.data(), summary.name.data(), summary.name.size());
        expected_.name = {name_.data(), summary.name.size()};
        source_ = source;
        request_ = request;
        generation_ = generation;
        phase_ = prepared == DownloadOperationResult::Pending ? DownloadPhase::Submitting : DownloadPhase::Waiting;
        return true;
    }
    bool begin(const Destination& source, const RequestId& request,
               const protocol::SummaryView& summary, std::uint64_t generation)
    {
        if (phase_ != DownloadPhase::Idle || generation == 0 || summary.name.size() > kMaxNameBytes ||
            !protocol::validRecordText(summary.name, false, true)) return false;
        std::size_t size = 0;
        if (!protocol::encodeGetRequest(request, summary.id, &summary.hash, nullptr, 8192,
                                        request_bytes_.data(), request_bytes_.size(), size)) return false;
        expected_ = summary;
        name_.fill(0);
        std::memcpy(name_.data(), summary.name.data(), summary.name.size());
        expected_.name = {name_.data(), summary.name.size()};
        source_ = source;
        request_ = request;
        generation_ = generation;
        const auto result = port_.submit(source_, request_, {request_bytes_.data(), size});
        if (result == DownloadOperationResult::Rejected) return false;
        phase_ = result == DownloadOperationResult::Pending ? DownloadPhase::Submitting : DownloadPhase::Waiting;
        return true;
    }
    // Authenticated transport event only; local destination checked by owner.
    bool accept(const Destination& source, ByteView response, uint8_t* scratch, size_t capacity)
    {
        if (phase_ != DownloadPhase::Waiting || source.bytes != source_.bytes) return false;
        if (!scratch) return false;
        const auto input = reinterpret_cast<uintptr_t>(response.data), output = reinterpret_cast<uintptr_t>(scratch);
        if (input <= output ? output - input < response.size : input - output < capacity) return false;
        protocol::GetResponseView parsed;
        protocol::VerifiedRecordView verified;
        if (!protocol::decodeGetResponse(response, request_, 8192, parsed) || parsed.has_conflict ||
            protocol::verifyGeocache(parsed.signed_cache, crypto_, scratch, capacity, verified,
                                     &expected_.id, &expected_.hash) != protocol::VerificationResult::Valid) return false;
        const auto& r = verified.record;
        if (r.revision != expected_.revision || r.state != expected_.state ||
            r.latitude_e7 != expected_.latitude_e7 || r.longitude_e7 != expected_.longitude_e7 ||
            r.name != expected_.name || r.difficulty_x2 != expected_.difficulty_x2 ||
            r.terrain_x2 != expected_.terrain_x2 || r.container_size != expected_.container_size ||
            parsed.signed_cache.size != expected_.signed_bytes) return false;
        const auto result = port_.commit(source_, request_, generation_, response, verified);
        if (result == DownloadOperationResult::Rejected) return false;
        phase_ = result == DownloadOperationResult::Pending ? DownloadPhase::Installing : DownloadPhase::Stored;
        return result == DownloadOperationResult::Complete;
    }
    void advance()
    {
        if (phase_ != DownloadPhase::Submitting && phase_ != DownloadPhase::Installing && phase_ != DownloadPhase::Cancelling) return;
        const auto result = port_.poll();
        if (result == DownloadOperationResult::Pending) return;
        if (result == DownloadOperationResult::Rejected)
        {
            phase_ = DownloadPhase::Failed;
            return;
        }
        phase_ = phase_ == DownloadPhase::Submitting ? DownloadPhase::Waiting : phase_ == DownloadPhase::Installing ? DownloadPhase::Stored
                                                                                                                    : DownloadPhase::Cancelled;
    }
    bool cancel()
    {
        if (phase_ == DownloadPhase::Stored) return false;
        if (phase_ == DownloadPhase::Cancelled || phase_ == DownloadPhase::Cancelling) return true;
        if (phase_ == DownloadPhase::Idle)
        {
            phase_ = DownloadPhase::Cancelled;
            return true;
        }
        const auto result = port_.cancel(source_, request_, generation_);
        if (result == DownloadOperationResult::Rejected) return false;
        phase_ = result == DownloadOperationResult::Pending ? DownloadPhase::Cancelling : DownloadPhase::Cancelled;
        return true;
    }
    DownloadPhase phase() const { return phase_; }

  private:
    DownloadPort& port_;
    protocol::RecordCrypto& crypto_;
    Destination source_;
    RequestId request_;
    protocol::SummaryView expected_;
    std::array<char, kMaxNameBytes + 1> name_{};
    std::array<std::uint8_t, 128> request_bytes_{};
    std::uint64_t generation_ = 0;
    DownloadPhase phase_ = DownloadPhase::Idle;
};
static_assert(sizeof(DownloadClient) <= 512, "Download clients must not own a full-record crypto workspace");
} // namespace geocaching
