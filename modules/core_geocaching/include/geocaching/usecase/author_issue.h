#pragma once
#include "geocaching/protocol/record_decoder.h"
#include <array>
#include <cstring>

namespace geocaching
{
enum class AuthorReservationResult : uint8_t
{
    Pending,
    Reserved,
    Failed
};
class AuthorIssuePort
{
  public:
    virtual ~AuthorIssuePort() = default;
    virtual bool getAuthorKey(uint8_t out[64]) = 0;
    virtual AuthorReservationResult reserve(ByteView encoded, uint8_t* workspace, size_t capacity) = 0;
    virtual void cancelReservation() {}
    // The output may alias workspace; encoded must remain disjoint. Consume
    // scratch synchronously and do not retain it after this call.
    virtual bool sign(ByteView encoded, uint8_t* workspace, size_t capacity,
                      uint8_t* output, size_t output_capacity, size_t& written) = 0;
};
enum class AuthorIssuePhase : uint8_t
{
    Idle,
    CheckAuthor,
    Reserve,
    Sign,
    Signed,
    Failed,
    Cancelled
};

// Small serialized job; no owned payload copies or allocation. The owner pins
// the immutable draft record until a terminal phase, and leases one buffer of
// record.size + 70 bytes after memory admission. That buffer is scratch during
// reservation/signing, then SignedCache output; keep it until the result is
// consumed. Signed is not published or network accepted.
class AuthorIssue
{
  public:
    explicit AuthorIssue(AuthorIssuePort& port) : port_(port) {}
    AuthorIssue(const AuthorIssue&) = delete;
    AuthorIssue& operator=(const AuthorIssue&) = delete;
    bool begin(ByteView record, uint8_t* workspace, size_t capacity)
    {
        if (phase_ != AuthorIssuePhase::Idle || !record.data || record.size > kMaxRecordBytes ||
            !workspace || capacity < record.size + 70) return false;
        // A borrowed draft cannot share bytes with the mutable operation lease.
        const auto input = reinterpret_cast<uintptr_t>(record.data);
        const auto scratch = reinterpret_cast<uintptr_t>(workspace);
        if (input <= scratch ? scratch - input < record.size : input - scratch < capacity) return false;
        RecordView decoded;
        if (!protocol::decodeGeocacheRecord(record, decoded)) return false;
        encoded_ = record;
        author_ = decoded.author_public_key;
        workspace_ = workspace;
        capacity_ = capacity;
        phase_ = AuthorIssuePhase::CheckAuthor;
        return true;
    }
    AuthorIssuePhase phase() const { return phase_; }
    ByteView signedRecord() const { return phase_ == AuthorIssuePhase::Signed ? ByteView{workspace_, signed_size_} : ByteView{}; }
    void cancel()
    {
        if (phase_ == AuthorIssuePhase::Reserve) port_.cancelReservation();
        if (phase_ != AuthorIssuePhase::Signed && phase_ != AuthorIssuePhase::Failed) phase_ = AuthorIssuePhase::Cancelled;
    }
    void advance()
    {
        const ByteView record = encoded_;
        if (phase_ == AuthorIssuePhase::CheckAuthor)
        {
            std::array<uint8_t, 64> current{};
            phase_ = port_.getAuthorKey(current.data()) && !std::memcmp(current.data(), author_.data, current.size()) ? AuthorIssuePhase::Reserve : AuthorIssuePhase::Failed;
        }
        else if (phase_ == AuthorIssuePhase::Reserve)
        {
            const auto result = port_.reserve(record, workspace_, capacity_);
            if (result == AuthorReservationResult::Reserved) phase_ = AuthorIssuePhase::Sign;
            else if (result == AuthorReservationResult::Failed) phase_ = AuthorIssuePhase::Failed;
        }
        else if (phase_ == AuthorIssuePhase::Sign)
        {
            signed_size_ = 0;
            if (!port_.sign(record, workspace_, capacity_, workspace_, capacity_, signed_size_) ||
                signed_size_ > capacity_)
            {
                phase_ = AuthorIssuePhase::Failed;
                return;
            }
            protocol::CmpReader reader({workspace_, signed_size_});
            size_t count = 0;
            ByteView preserved, signature;
            const bool valid = reader.array(count, 2) && count == 2 && reader.binary(preserved, kMaxRecordBytes) &&
                               preserved.size == encoded_.size && !std::memcmp(preserved.data, encoded_.data, encoded_.size) &&
                               reader.binary(signature, 64) && signature.size == 64 && reader.finished();
            phase_ = valid ? AuthorIssuePhase::Signed : AuthorIssuePhase::Failed;
        }
    }

  private:
    AuthorIssuePort& port_;
    ByteView encoded_, author_;
    uint8_t* workspace_ = nullptr;
    size_t capacity_ = 0, signed_size_ = 0;
    AuthorIssuePhase phase_ = AuthorIssuePhase::Idle;
};
static_assert(sizeof(AuthorIssue) <= 128, "Author signing jobs retain metadata, not record copies");
} // namespace geocaching
