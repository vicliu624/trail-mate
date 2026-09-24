#pragma once
#include "geocaching/storage/pending_request.h"
#include "geocaching/storage/tx_attempt.h"
#include "platform/esp/arduino_common/geocaching/sd_journal.h"

namespace platform::esp::arduino_common::geocaching
{
enum class DispatchReadResult : uint8_t
{
    Ready,
    None,
    Pending,
    Corrupt
};
struct DispatchSendView
{
    ::geocaching::ByteView request;
    bool stopped = false;
};
// Serialized storage owner. Reads may yield; returned bytes remain borrowed
// until the next storage operation. Persistence progresses through stepCommit;
// implementations must respect the serialized owner's per-step I/O budget.
class RequestDispatchStore
{
  public:
    virtual ~RequestDispatchStore() = default;
    virtual bool needsRecovery() const = 0;
    virtual bool commitPending() const = 0;
    virtual JournalWriteResult stepCommit() = 0;
    virtual DispatchReadResult readPending(const ::geocaching::Destination&, ::geocaching::ByteView,
                                           ::geocaching::storage::PendingRequestView&) = 0;
    virtual DispatchReadResult readForSend(::geocaching::ByteView, DispatchSendView&) = 0;
    virtual JournalWriteResult beginAttempt(const ::geocaching::Destination&, ::geocaching::ByteView,
                                            const std::array<uint8_t, 16>&, const ::geocaching::storage::StoredTime&) = 0;
    virtual JournalWriteResult recordAttemptHash(::geocaching::ByteView, const std::array<uint8_t, 32>&) = 0;
    virtual JournalWriteResult finishAttempt(::geocaching::ByteView, ::geocaching::storage::TxAttemptState,
                                             const ::geocaching::storage::StoredTime&) = 0;
    virtual JournalWriteResult expireOneAttempt(const ::geocaching::storage::StoredTime&, uint64_t, uint64_t, bool&) = 0;
    // Legacy synchronous scanning yields only when it found an expiry. An
    // asynchronous implementation reports whether its completed scan changed it.
    virtual bool expirationChanged() const { return true; }
};
} // namespace platform::esp::arduino_common::geocaching
