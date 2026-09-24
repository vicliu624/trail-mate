#pragma once
#include "geocaching/usecase/author_issue.h"
#include "platform/esp/arduino_common/geocaching/publication_store.h"

namespace platform::esp::arduino_common::geocaching
{
// Owned by the serialized storage worker. Transport is the existing router;
// templating permits the same persistence boundary to be tested without radio.
template <class Transport>
class SdAuthorIssuePort final : public ::geocaching::AuthorIssuePort
{
  public:
    SdAuthorIssuePort(Transport& transport, PublicationStore& store,
                      ::geocaching::protocol::RecordCrypto& crypto,
                      const ::geocaching::storage::StoredTime& issued, ::geocaching::ByteView draft = {}, uint64_t generation = 0)
        : store_(store), transport_(transport), crypto_(crypto), issued_(issued), draft_(draft), generation_(generation) {}

    bool getAuthorKey(uint8_t out[64]) override { return transport_.getGeocachingAuthorKey(out); }

    ::geocaching::AuthorReservationResult reserve(::geocaching::ByteView record, uint8_t* workspace, size_t capacity) override
    {
        using Result = ::geocaching::AuthorReservationResult;
        if (failed_) return Result::Failed;
        if (reserved_) return Result::Reserved;
        const auto result = committing_ ? store_.stepCommit() : draft_.size ? store_.reserveDraftUnsignedRecord(draft_, generation_, record, crypto_, workspace, capacity, issued_)
                                                                            : store_.reserveUnsignedRecord(record, crypto_, workspace, capacity, issued_);
        if (result == JournalWriteResult::Busy) return Result::Pending;
        if (result == JournalWriteResult::InProgress)
        {
            committing_ = true;
            return Result::Pending;
        }
        committing_ = false;
        reserved_ = result == JournalWriteResult::Verified;
        failed_ = !reserved_;
        return reserved_ ? Result::Reserved : Result::Failed;
    }

    void cancelReservation() override
    {
        if (committing_) store_.cancelCommit();
        committing_ = false;
        failed_ = true;
    }

    bool sign(::geocaching::ByteView record, uint8_t* workspace, size_t capacity,
              uint8_t* output, size_t output_capacity, size_t& written) override
    {
        written = 0;
        if (!reserved_ || failed_ || committing_ || store_.needsRecovery()) return false;
        return transport_.signGeocachingRecord({record.data, record.size}, workspace, capacity, output, output_capacity, written);
    }

  private:
    PublicationStore& store_;
    Transport& transport_;
    ::geocaching::protocol::RecordCrypto& crypto_;
    ::geocaching::storage::StoredTime issued_;
    ::geocaching::ByteView draft_;
    uint64_t generation_ = 0;
    bool committing_ = false, reserved_ = false, failed_ = false;
};
} // namespace platform::esp::arduino_common::geocaching
