#pragma once
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/draft_catalog.h"
#include "geocaching/storage/publication_history.h"
#include "geocaching/storage/publication_recovery.h"
#include "geocaching/storage/queued_request.h"
#include "platform/esp/arduino_common/geocaching/sd_journal.h"

namespace platform::esp::arduino_common::geocaching
{
enum class DraftReadResult : uint8_t
{
    Pending,
    Ready,
    NotFound,
    Busy,
    Invalid,
    IoError,
    VolumeChanged,
    WorkspaceTooSmall,
    Unavailable
};
// Serialized owner interface shared by the author and publication ports.
// Reservation borrows the immutable record and encoding workspace until its
// terminal step; AuthorIssue already holds those leases through signing.
// Request/response calls must consume callback-owned bytes before returning.
class PublicationStore
{
  public:
    virtual ~PublicationStore() = default;
    virtual bool needsRecovery() const = 0;
    virtual bool commitPending() const = 0;
    virtual JournalWriteResult stepCommit() = 0;
    virtual JournalWriteResult cancelCommit() = 0;
    // Storage-worker calls only. Ready borrows one row until releaseDraftRead;
    // repeat the same key while Pending, and do not advance another operation.
    virtual DraftReadResult readDraft(::geocaching::ByteView key, ::geocaching::ByteView& value) = 0;
    virtual void releaseDraftRead() = 0;
    // Ready lends persisted request/result bytes until releaseDraftRead. The
    // output object need not survive Pending; filters are copied by the owner.
    virtual DraftReadResult readPublicationRecovery(const ::geocaching::storage::PublicationRecoveryFilter&,
                                                    ::geocaching::storage::PublicationRecoveryView&) = 0;
    // Repeat the same output object/offset while Pending. It is publishable
    // only at Ready; cancellation uses releaseDraftRead from the worker.
    virtual DraftReadResult readDraftCatalog(size_t offset, ::geocaching::protocol::RecordCrypto&, ::geocaching::storage::DraftCatalogPage&) = 0;
    // Repeat unchanged arguments/output while Pending. Ready owns all metadata;
    // cancellation shares releaseDraftRead with the other read operations.
    virtual DraftReadResult readPublicationHistory(::geocaching::ByteView key, uint64_t generation,
                                                   const ::geocaching::GeocacheId&, ::geocaching::ByteView author,
                                                   ::geocaching::storage::PublicationHistory&) = 0;
    // Mutable editor command owns editable fields only. Storage restores the
    // committed author/base hash; retain its full capacity until inputConsumed.
    virtual JournalWriteResult editDraft(::geocaching::ByteView key, uint8_t* bytes, size_t size, size_t capacity, uint64_t expected) = 0;
    virtual bool inputConsumed() const = 0;
    virtual JournalWriteResult saveDraft(::geocaching::ByteView key, ::geocaching::ByteView encoded, uint64_t expected) = 0;
    virtual JournalWriteResult reserveUnsignedRecord(::geocaching::ByteView, ::geocaching::protocol::RecordCrypto&,
                                                     uint8_t*, size_t, const ::geocaching::storage::StoredTime&) = 0;
    virtual JournalWriteResult reserveDraftUnsignedRecord(::geocaching::ByteView, uint64_t, ::geocaching::ByteView,
                                                          ::geocaching::protocol::RecordCrypto&, uint8_t*, size_t,
                                                          const ::geocaching::storage::StoredTime&) = 0;
    virtual JournalWriteResult persistNewTask(const ::geocaching::Destination&, const ::geocaching::Destination&,
                                              const ::geocaching::RequestId&, const std::array<uint8_t, 16>&, uint8_t,
                                              ::geocaching::ByteView, const ::geocaching::storage::StoredTime&,
                                              const ::geocaching::storage::RequestTaskTarget&) = 0;
    virtual JournalWriteResult commitPublishResult(const ::geocaching::Destination&, const ::geocaching::Destination&,
                                                   const ::geocaching::RequestId&, ::geocaching::ByteView,
                                                   ::geocaching::protocol::RecordCrypto&) = 0;
    virtual JournalWriteResult stopTask(const std::array<uint8_t, 16>&) = 0;
};
} // namespace platform::esp::arduino_common::geocaching
