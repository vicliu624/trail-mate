#pragma once
#include "geocaching/protocol/verify_record.h"
#include "geocaching/storage/download_recovery.h"
#include "geocaching/storage/queued_request.h"
#include "geocaching/storage/saved_cache.h"
#include "platform/esp/arduino_common/geocaching/sd_journal.h"

namespace platform::esp::arduino_common::geocaching
{
enum class DownloadRecoveryRead : uint8_t
{
    Pending,
    Ready,
    End,
    Busy,
    Unavailable,
    WorkspaceTooSmall,
    IoError,
    VolumeChanged,
    Invalid
};
// Read accessors never perform SD I/O. readDownload() advances one bounded
// step until Verified; its owner pins record bytes through GPX staging. A
// mutation can reuse that lease only after the staging consumer is destroyed.
class DownloadStore
{
  public:
    virtual ~DownloadStore() = default;
    virtual bool needsRecovery() const = 0;
    // exact: key is one cache ID; otherwise key is empty or the last returned
    // ID (exclusive, bytewise order). Ready owns all metadata and releases the
    // read lease. End means no installed candidate, never a storage error.
    virtual DownloadRecoveryRead readSavedCache(::geocaching::ByteView key, bool exact,
                                                ::geocaching::protocol::RecordCrypto& crypto,
                                                ::geocaching::storage::SavedCacheRecord& out) = 0;
    // Ready pins the selected head until persistNewTask() consumes the lease,
    // or releaseRead() cancels it. A zero generation means the counter is full.
    virtual DownloadRecoveryRead readNextGeneration(const ::geocaching::GeocacheId& id, uint64_t& generation) = 0;
    // Stable-key boot cursor. Pending pins the root/frame; Ready copies owned
    // metadata and releases the lease before the installation acquires it.
    // releaseRead() cancels a Pending selection without altering durable intent.
    virtual DownloadRecoveryRead readRecovery(::geocaching::ByteView after, ::geocaching::storage::DownloadRecoveryRequest& out) = 0;
    // Ready keeps the preview's borrowed name pinned until releaseRead(). Copy
    // it before preparing the download port, which needs the same workspace.
    virtual DownloadRecoveryRead readWaitingDownload(const ::geocaching::Destination& local, ::geocaching::storage::DownloadRecoveryRequest& out,
                                                     ::geocaching::protocol::SummaryView& preview) = 0;
    virtual JournalWriteResult readDownload(::geocaching::ByteView key, uint64_t generation) = 0;
    virtual void releaseRead() = 0;
    virtual bool readOutgoing(::geocaching::ByteView key, ::geocaching::storage::OutgoingView& out) const = 0;
    virtual bool readInstall(const std::array<uint8_t, 16>& task, ::geocaching::ByteView& out) const = 0;
    virtual bool verifyRecord(::geocaching::ByteView signed_cache, ::geocaching::protocol::RecordCrypto& crypto,
                              const ::geocaching::GeocacheId& id, const ::geocaching::RevisionHash& hash,
                              ::geocaching::protocol::VerifiedRecordView& out) = 0;
    virtual bool downloadIntentActive(::geocaching::ByteView key, uint64_t generation) const = 0;
    virtual bool downloadCompleted(::geocaching::ByteView key, uint64_t generation) const = 0;
    virtual bool installedFileProof(::geocaching::ByteView key, uint64_t generation,
                                    std::array<uint8_t, 32>& hash, ::geocaching::RevisionHash& revision) const = 0;
    virtual bool commitPending() const = 0;
    virtual JournalWriteResult stepCommit() = 0;
    virtual JournalWriteResult persistNewTask(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                              const ::geocaching::RequestId& request, const std::array<uint8_t, 16>& task,
                                              uint8_t kind, ::geocaching::ByteView bytes, const ::geocaching::storage::StoredTime& time,
                                              const ::geocaching::storage::RequestTaskTarget& target) = 0;
    virtual JournalWriteResult recordDownloadResponse(const ::geocaching::Destination& local, const ::geocaching::Destination& remote,
                                                      const ::geocaching::RequestId& request, uint64_t generation,
                                                      ::geocaching::ByteView response, ::geocaching::protocol::RecordCrypto& crypto) = 0;
    virtual JournalWriteResult stopTask(const std::array<uint8_t, 16>& task) = 0;
    virtual JournalWriteResult prepareDownloadInstall(::geocaching::ByteView request, const std::array<uint8_t, 16>& task,
                                                      uint64_t generation, const std::array<uint8_t, 32>& hash,
                                                      ::geocaching::ByteView old_hash, ::geocaching::protocol::RecordCrypto& crypto) = 0;
    virtual JournalWriteResult finishDownloadInstall(::geocaching::ByteView request, const std::array<uint8_t, 16>& task,
                                                     uint64_t generation, const std::array<uint8_t, 32>& observed_hash) = 0;
};
} // namespace platform::esp::arduino_common::geocaching
