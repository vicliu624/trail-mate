#pragma once

#include "hostlink/chat_submission_owner.h"
#include <limits>

namespace hostlink::chat_management
{
constexpr uint8_t kJournalSlots = 16;
enum class SlotResult : uint8_t
{
    Empty,
    Found,
    Unavailable
};

// One atomic durable value per slot. The transport must distinguish verified
// absence from an I/O failure, and must never report an ambiguous write as OK.
class JournalIo
{
  public:
    virtual ~JournalIo() = default;
    virtual SlotResult read(uint8_t slot, SubmissionRecord& record) = 0;
    virtual bool write(uint8_t slot, const SubmissionRecord& record) = 0;
};

// Bounded metadata retention. No unresolved reservation is ever evicted.
// Each poll reads one slot, so a slow backend cannot turn a list scan into
// sixteen storage operations on the application loop in one invocation.
class SubmissionJournal
{
  public:
    explicit SubmissionJournal(JournalIo& io) : io_(io) {}
    LookupResult lookup(const SubmissionRecord& key, SubmissionRecord& result)
    {
        if (!scanning_)
        {
            scanning_ = true;
            index_ = 0;
            available_ = kJournalSlots;
            oldest_ = UINT32_MAX;
            maximum_ = 0;
            empty_ = false;
        }
        const auto read = io_.read(index_, scratch_);
        if (read == SlotResult::Unavailable)
        {
            scanning_ = false;
            return LookupResult::Unavailable;
        }
        if (read == SlotResult::Found)
        {
            if (std::memcmp(key.store, scratch_.store, kIdSize) != 0)
            {
                scanning_ = false;
                return LookupResult::Unavailable;
            }
            if (std::memcmp(key.owner, scratch_.owner, kIdSize) == 0 &&
                std::memcmp(key.operation, scratch_.operation, kIdSize) == 0)
            {
                result = scratch_;
                scanning_ = false;
                return LookupResult::Found;
            }
            if (scratch_.revision > maximum_) maximum_ = scratch_.revision;
            const bool terminal = scratch_.state == SubmissionState::Submitted || scratch_.state == SubmissionState::Rejected;
            if (terminal && scratch_.revision < oldest_)
            {
                oldest_ = scratch_.revision;
                if (!empty_) available_ = index_;
            }
        }
        else if (!empty_)
        {
            empty_ = true;
            available_ = index_;
        }
        if (++index_ < kJournalSlots) return LookupResult::Pending;
        scanning_ = false;
        selected_ = available_;
        empty_ = false;
        return LookupResult::Missing;
    }
    ReservationResult reserve(const SubmissionRecord& record)
    {
        if (selected_ >= kJournalSlots || maximum_ == UINT32_MAX) return ReservationResult::Rejected;
        scratch_ = record;
        scratch_.revision = maximum_ + 1;
        return io_.write(selected_, scratch_) ? ReservationResult::Created : ReservationResult::Ambiguous;
    }
    CommitResult commit(SubmissionRecord& record)
    {
        record.revision = maximum_ + 1;
        scratch_ = record;
        return selected_ < kJournalSlots && io_.write(selected_, scratch_) ? CommitResult::Durable : CommitResult::Unavailable;
    }
    uint32_t nextRevision() const { return maximum_ == UINT32_MAX ? 0 : maximum_ + 1; }

  private:
    JournalIo& io_;
    SubmissionRecord scratch_{};
    uint32_t oldest_ = UINT32_MAX;
    uint32_t maximum_ = 0;
    uint8_t index_ = 0;
    uint8_t available_ = kJournalSlots;
    uint8_t selected_ = kJournalSlots;
    bool scanning_ = false;
    bool empty_ = false;
};
} // namespace hostlink::chat_management
