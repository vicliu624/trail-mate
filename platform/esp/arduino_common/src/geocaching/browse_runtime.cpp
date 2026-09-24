#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "app/app_context.h"
#include "geocaching/protocol/record_encoder.h"
#include "geocaching/storage/download_recovery.h"
#include "geocaching/storage/draft_publication.h"
#include "geocaching/storage/record_shape.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
#include "platform/esp/arduino_common/geocaching/author_issue_port.h"
#include "platform/esp/arduino_common/geocaching/indexed_dispatch_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_download_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_publication_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_query_store_port.h"
#include "platform/esp/arduino_common/geocaching/query_browse_source.h"
#include "platform/esp/arduino_common/geocaching/request_dispatcher.h"
#include "platform/esp/arduino_common/geocaching/saved_cache_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_rotation.h"
#include "platform/esp/arduino_common/geocaching/sd_download_port.h"
#include "platform/esp/arduino_common/geocaching/sd_index_repair.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"
#include "platform/esp/arduino_common/geocaching/sd_publish_port.h"
#include "platform/esp/common/geocaching_crypto.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/esp/common/meshcore_runtime_compat.h"
#include "ui/screens/geocaching/geocaching_page_shell.h"
#include <Arduino.h>
#include <atomic>
#include <ctime>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <memory>
#include <new>

namespace platform::esp::arduino_common::geocaching::browse_runtime
{
namespace
{
namespace gc = ::geocaching;
namespace mem = ::platform::esp::common::memory;
using Digest = ::platform::esp::common::meshcore_runtime::Sha256Digest;
constexpr size_t kFrameCapacity = 8192, kEncodingCapacity = 8192, kPayloadCapacity = 8192;
constexpr size_t kVerificationCapacity = gc::kMaxRecordBytes + 64, kPageCapacity = 2048;
constexpr gc::protocol::QueryRegion kWorld{-900000000, -1800000000, 900000000, 1800000000};
enum class Phase : uint8_t
{
    Inspect,
    CheckNew,
    Directories,
    OpenFormat,
    WriteFormat,
    FlushFormat,
    CloseFormat,
    VerifyFormat,
    Recover,
    ResumeDownloads,
    Connect,
    Ready,
    Failed
};
struct Announcement
{
    gc::Destination discovery, delivery;
    std::array<uint8_t, 64> key{};
    std::array<uint8_t, 128> data{};
    size_t size = 0;
};
// One producer (callbacks serialized by the mesh router) and one consumer
// (the storage owner). Keep the first pending announcement until consumed.
// No session mutex or allocation is needed by the network callback.
Announcement pending_announcement;
std::atomic<bool> announcement_pending{false};
static_assert(sizeof(Announcement) <= 240);
// Router callbacks are serialized. The storage owner consumes this one-slot
// mailbox without sharing its long-lived session/SD mutex with the producer.
// A full mailbox applies backpressure: no receipt is acknowledged or replaced.
struct PendingReply
{
    gc::Destination source, destination;
    uint8_t* bytes = nullptr;
    size_t size = 0;
    ~PendingReply() { heap_caps_free(bytes); }
};
std::atomic<PendingReply*> pending_reply{nullptr};
struct Session
{
    struct Publication
    {
        enum class DraftStage : uint8_t
        {
            None,
            Bind,
            Binding,
            Encode,
            Sign
        };
        DraftStage draft_stage = DraftStage::None;
        std::array<uint8_t, 16> draft_id{};
        std::array<uint8_t, 64> author{};
        uint64_t draft_generation = 0;
        uint32_t expected_revision = 0;
        gc::storage::StoredTime issued;
        uint8_t* unsigned_bytes = nullptr;
        size_t unsigned_size = 0;
        uint8_t* draft_bytes = nullptr;
        size_t draft_size = 0;
        gc::GeocacheId cache;
        gc::storage::PublicationHistory history;
        std::unique_ptr<DeviceAuthorIssuePort> author_port;
        std::unique_ptr<gc::AuthorIssue> issue;
        const char* error = nullptr;
        uint8_t* bytes = nullptr;
        size_t size = 0;
        gc::Destination remote;
        std::unique_ptr<SdPublishPort> port;
        std::unique_ptr<gc::PublishAttempt> attempt;
        uint64_t started = 0;
        uint32_t wait_ms = gc::QueryClient::kReplyTimeoutMs;
        ~Publication()
        {
            heap_caps_free(bytes);
            heap_caps_free(unsigned_bytes);
            heap_caps_free(draft_bytes);
        }
    };
    std::unique_ptr<Publication> publication;
    // Recovery discovers work left by a previous session. New work in this
    // session already has an owner; unrelated query commits must not rescan it.
    bool publication_recovery_complete = false;
    bool publication_restore_pending = false, publication_restore_background = false;
    struct DraftSave
    {
        std::array<uint8_t, 16> id{};
        uint8_t* bytes = nullptr;
        size_t size = 0, capacity = 0;
        uint64_t expected = 0;
        bool started = false, done = false, saved = false, editor_fields = true;
        ~DraftSave() { heap_caps_free(bytes); }
    };
    std::unique_ptr<DraftSave> draft_save;
    struct DraftRead
    {
        std::array<uint8_t, 16> id{};
        uint8_t* bytes = nullptr;
        size_t size = 0;
        ::ui::geocaching::DraftReadStatus status = ::ui::geocaching::DraftReadStatus::Pending;
        ~DraftRead() { heap_caps_free(bytes); }
    };
    std::unique_ptr<DraftRead> draft_read;
    bool draft_io_reset = false;
    struct DraftCatalog
    {
        gc::storage::DraftCatalogPage page;
        size_t requested_offset = 0, total = 0;
        uint64_t sequence = UINT64_MAX, generation = 1;
        bool reading = false, ready = false, failed = false;
    };
    std::unique_ptr<DraftCatalog> draft_catalog;
    bool draft_catalog_wanted = false;
    Phase phase = Phase::Inspect;
    const char* status = "Opening geocaching storage...";
    const char* notice = nullptr;
    gc::storage::VolumeInstance volume{};
    storage::SdRuntimeFile format;
    size_t directory = 0;
    uint64_t connect_since = 0;
    bool mkdir_pending = false;
    // Roots and the current query page survive between operations. All other
    // byte buffers belong to the single active storage lease and are trimmed
    // after the maintenance step that consumed its final borrowed views.
    std::array<gc::storage::IndexRootBytes, 2> roots{};
    gc::storage::IndexRootView root;
    unsigned root_copy = 0;
    IndexWorkspaceOwner workspace_owner;
    gc::storage::QueuedRequestWorkspace workspace{nullptr, kEncodingCapacity};
    uint8_t *frame = nullptr, *encoded = nullptr, *payload = nullptr, *verification = nullptr;
    uint8_t *query_page = nullptr, *response = nullptr;
    bool workspace_unavailable = false;
    size_t response_size = 0;
    gc::Destination response_source;
    gc::Destination local;
    gc::RequestId response_id;
    uint8_t response_operation = 0;
    std::array<gc::storage::MutationView, 3> mutations{};
    std::unique_ptr<SdIndexRepair<Digest>> recovery;
    std::unique_ptr<SdCheckpointRotation<Digest>> checkpoint;
    struct PreviousQueries
    {
        explicit PreviousQueries(const gc::storage::VolumeInstance& volume) : scan(volume) {}
        gc::storage::IndexRootBytes snapshot{};
        SdIndexScan scan;
        std::unique_ptr<SdIndexedStopTask> stop;
        bool started = false;
        size_t stopped = 0;
    };
    std::unique_ptr<PreviousQueries> previous_queries;
    bool previous_queries_retired = false;
    uint64_t checkpoint_attempt_sequence = 0;
    bool checkpoint_recovery_required = false;
    bool load_more_pending = false;
    uint64_t load_more_generation = 0;
    std::unique_ptr<IndexedPublicationStore> store;
    std::unique_ptr<IndexedDispatchStore> dispatch_store;
    std::unique_ptr<IndexedQueryStorePort> port;
    std::unique_ptr<gc::QueryClient> client;
    std::unique_ptr<RequestDispatcher> dispatcher;
    std::unique_ptr<QueryBrowseSource> source;
    std::unique_ptr<SavedCacheCatalog<Digest>> saved;
    ::platform::esp::common::EspGeocachingCrypto crypto;
    std::unique_ptr<IndexedDownloadStore> download_store;
    struct DownloadStart
    {
        gc::protocol::SummaryView summary;
        std::array<char, gc::kMaxNameBytes> name{};
        gc::Destination remote;
    };
    std::unique_ptr<DownloadStart> download_start;
    const char* download_start_error = nullptr;
    bool download_recovery_complete = false;
    bool download_restore_pending = false;
    std::unique_ptr<SdDownloadPort<Digest>> download_port;
    std::unique_ptr<gc::DownloadClient> download;
    std::array<uint8_t, 48> recovered_download{};
    bool have_recovered_download = false;
    bool recovering_installed_download = false, recovery_attention = false;
    uint64_t download_started = 0;
    uint32_t download_wait_ms = gc::QueryClient::kReplyTimeoutMs;
    size_t download_scratch = 0;
    chat::IMeshAdapter* created_backend = nullptr;
    bool ensureBuffers(bool operations)
    {
        const size_t missing = (!frame ? kFrameCapacity : 0) + (!encoded ? kEncodingCapacity : 0) +
                               (operations && !payload ? kPayloadCapacity : 0) + (operations && !verification ? kVerificationCapacity : 0);
        if (missing && !mem::admit("geocaching.index.io", 4096, 0, missing, 40 * 1024, 0))
        {
            workspace_unavailable = true;
            return false;
        }
        if (!frame) frame = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.read", kFrameCapacity, false));
        if (!encoded) encoded = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.encode", kEncodingCapacity, false));
        if (operations && !payload) payload = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.payload", kPayloadCapacity, false));
        if (operations && !verification) verification = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.verify", kVerificationCapacity, false));
        workspace.outgoing = encoded;
        workspace_unavailable = !frame || !encoded || (operations && (!payload || !verification));
        return !workspace_unavailable;
    }
    static bool prepareWorkspace(void* context, const void* owner)
    {
        auto& s = *static_cast<Session*>(context);
        const bool needs_record = owner == s.store.get() || owner == s.download_store.get();
        if (!s.ensureBuffers(needs_record)) return false;
        if (s.store) s.store->bindWorkspace(s.frame, s.payload, s.verification);
        if (s.download_store) s.download_store->bindWorkspace(s.frame, s.payload, s.verification);
        if (s.dispatch_store) s.dispatch_store->bindWorkspace(s.frame);
        if (s.port) s.port->bindWorkspace(s.frame);
        return true;
    }
    void trimBuffers()
    {
        if (recovery || workspace_owner.holder()) return;
        heap_caps_free(frame);
        heap_caps_free(encoded);
        heap_caps_free(payload);
        heap_caps_free(verification);
        frame = encoded = payload = verification = workspace.outgoing = nullptr;
    }
    bool needsRecovery() const
    {
        return checkpoint_recovery_required || (store && store->needsRecovery()) || (download_store && download_store->needsRecovery()) ||
               (dispatch_store && dispatch_store->needsRecovery()) || (port && port->needsRecovery());
    }
    ~Session()
    {
        workspace_owner.release(previous_queries.get());
        previous_queries.reset();
        workspace_owner.release(checkpoint.get());
        checkpoint.reset();
        if (store && store->commitPending()) store->cancelCommit();
        source.reset();
        saved.reset();
        download.reset();
        download_port.reset();
        download_store.reset();
        publication.reset();
        dispatcher.reset();
        dispatch_store.reset();
        client.reset();
        port.reset();
        store.reset();
        recovery.reset();
        heap_caps_free(response);
        heap_caps_free(query_page);
        trimBuffers();
    }
};
chat::MeshAdapterRouter* router = nullptr;
LoraBoard* board = nullptr;
SemaphoreHandle_t mutex = nullptr;
std::unique_ptr<Session> session;
std::array<uint8_t, 16> boot{};
std::atomic<bool> wanted{false}, active{false}, restart{false};
std::atomic<bool> cancel_draft_read{false};
std::atomic<uint32_t> next_step{0};
std::atomic<uint32_t> replies_seen{0}, replies_busy{0}, replies_queued{0};
uint32_t replies_processed = 0, replies_accepted = 0;
uint64_t epoch = 0;
// Dispatch consumes borrowed request bytes after releasing its read lease.
// Free the backing memory only when the entire maintenance slice returns.
struct WorkspaceSlice
{
    Session& session;
    const bool unavailable = session.workspace_unavailable;
    ~WorkspaceSlice()
    {
        session.trimBuffers();
        if (unavailable != session.workspace_unavailable) ++epoch;
        if (session.workspace_unavailable) next_step.store(millis() + 1000);
        const uint32_t now_ms = millis();
        static uint32_t reported_ms = 0;
        static unsigned reported_phase = ~0U;
        const unsigned query = session.client ? static_cast<unsigned>(session.client->phase()) : 255;
        const unsigned phase = (static_cast<unsigned>(session.phase) << 8) | query;
        if (phase == reported_phase && static_cast<uint32_t>(now_ms - reported_ms) < 60000) return;
        reported_ms = now_ms;
        reported_phase = phase;
        const auto& owner = session.workspace_owner;
        const char* lease = !owner.holder()                              ? "none"
                            : owner.heldBy(session.port.get())           ? "query"
                            : owner.heldBy(session.dispatch_store.get()) ? "dispatch"
                            : owner.heldBy(session.download_store.get()) ? "download"
                            : owner.heldBy(session.store.get())          ? "publication"
                                                                         : "other";
        Serial.printf("[Geocaching] state=%u query=%u sequence=%llu lease=%s dispatch=%u response=%u proof=%u restore_pub=%u restore_download=%u rx=%lu busy=%lu queued=%lu processed=%lu accepted=%lu\n",
                      static_cast<unsigned>(session.phase), query, static_cast<unsigned long long>(session.root.sequence), lease,
                      session.dispatch_store && session.dispatch_store->busy(), session.response != nullptr,
                      session.port && session.port->maintenancePending(), session.publication_restore_pending, session.download_restore_pending,
                      static_cast<unsigned long>(replies_seen.load()), static_cast<unsigned long>(replies_busy.load()),
                      static_cast<unsigned long>(replies_queued.load()), static_cast<unsigned long>(replies_processed),
                      static_cast<unsigned long>(replies_accepted));
    }
};
bool downloadActive()
{
    if (session && session->download_start) return true;
    if (!session || !session->download) return false;
    const auto phase = session->download->phase();
    return phase == gc::DownloadPhase::Submitting || phase == gc::DownloadPhase::Waiting ||
           phase == gc::DownloadPhase::Installing || phase == gc::DownloadPhase::Cancelling;
}
bool publicationActive()
{
    if (!session || !session->publication) return false;
    const auto& job = *session->publication;
    if (job.draft_stage != Session::Publication::DraftStage::None) return true;
    if (job.bytes) return true;
    if (!job.attempt) return false;
    const auto phase = job.attempt->phase();
    return phase == gc::PublishAttemptPhase::Submitting || phase == gc::PublishAttemptPhase::Waiting ||
           phase == gc::PublishAttemptPhase::Committing || phase == gc::PublishAttemptPhase::Cancelling;
}
bool draftSaveActive() { return session && session->draft_save && !session->draft_save->done; }
bool draftCatalogReady(const Session& s)
{
    return s.draft_catalog && s.draft_catalog->ready && s.store && !s.needsRecovery() &&
           s.draft_catalog->sequence == s.store->committedSequence() && s.draft_catalog->page.offset == s.draft_catalog->requested_offset;
}
bool advanceDraftCatalog(Session& s)
{
    if (!s.draft_catalog_wanted || !s.store || s.needsRecovery() || s.phase != Phase::Ready) return false;
    if (!s.draft_catalog) s.draft_catalog.reset(new (std::nothrow) Session::DraftCatalog);
    if (!s.draft_catalog) return false;
    auto& catalog = *s.draft_catalog;
    if (draftCatalogReady(s) || (catalog.failed && catalog.sequence == s.store->committedSequence())) return false;
    if (!catalog.reading)
    {
        catalog.sequence = s.store->committedSequence();
        catalog.ready = false;
        catalog.failed = false;
    }
    const auto result = s.store->readDraftCatalog(catalog.requested_offset, s.crypto, catalog.page);
    if (result == DraftReadResult::Busy) return false;
    catalog.reading = result == DraftReadResult::Pending;
    if (catalog.reading) return true;
    catalog.ready = result == DraftReadResult::Ready;
    catalog.failed = !catalog.ready;
    if (catalog.ready) catalog.total = catalog.page.total;
    ++catalog.generation;
    return true;
}
struct Guard
{
    bool locked = mutex && xSemaphoreTake(mutex, 0) == pdTRUE;
    ~Guard()
    {
        if (locked) xSemaphoreGive(mutex);
    }
};
gc::storage::StoredTime now(void*)
{
    gc::storage::StoredTime time;
    time.boot_id = boot;
    time.monotonic_ms = static_cast<uint64_t>(esp_timer_get_time()) / 1000;
    return time;
}
bool randomId(void*, uint8_t out[16])
{
    esp_fill_random(out, 16);
    return true;
}
void fail(const char* reason)
{
    if (session->saved) session->saved->releaseRead();
    if (session->store && session->store->commitPending()) session->store->cancelCommit();
    if (session->store) session->store->releaseDraftRead();
    session->recovery.reset();
    session->publication_restore_pending = false;
    if (session->draft_catalog)
    {
        session->draft_catalog->reading = session->draft_catalog->ready = false;
        session->draft_catalog->failed = true;
    }
    session->publication.reset();
    session->download.reset();
    session->download_port.reset();
    session->download_start.reset();
    if (session->download_store) session->download_store->releaseRead();
    session->download_restore_pending = false;
    session->phase = Phase::Failed;
    session->status = reason;
    ++epoch;
}

bool advanceCheckpoint(Session& s)
{
    if (!s.checkpoint) return false;
    const bool foreground = s.load_more_pending || s.response || downloadActive() || publicationActive() || draftSaveActive() ||
                            (s.saved && s.saved->pending()) ||
                            (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending) ||
                            (s.draft_catalog_wanted && !draftCatalogReady(s)) ||
                            (s.client && s.client->phase() != gc::QueryClientPhase::PageReady);
    if (foreground) s.checkpoint->yieldToForeground();
    const auto result = s.checkpoint->step();
    if (result == CheckpointRotationStep::Working) return true;
    if (result == CheckpointRotationStep::Busy || result == CheckpointRotationStep::Unavailable)
    {
        next_step.store(millis() + 1000);
        return true;
    }
    const bool complete = (result == CheckpointRotationStep::Complete || result == CheckpointRotationStep::Yielded) &&
                          s.checkpoint->selected(s.root, s.root_copy);
    s.workspace_owner.release(s.checkpoint.get());
    s.checkpoint.reset();
    if (!complete && result != CheckpointRotationStep::Deferred)
    {
        // Even an allocation failure may follow a published metadata copy.
        // Do not resume clients against their pre-rotation borrowed root.
        s.checkpoint_recovery_required = true;
        fail(result == CheckpointRotationStep::OutOfMemory ? "Storage maintenance needs more memory - reopen to retry"
                                                           : "Storage maintenance interrupted - reopen to recover");
    }
    return true;
}

bool startCheckpoint(Session& s)
{
    // Bound obsolete index growth without interrupting an active network
    // transaction. All consumers use the same workspace lease; acquiring it
    // proves their borrowed index cursors have been released.
    constexpr uint64_t interval = 256;
    if (!s.client || s.client->phase() != gc::QueryClientPhase::PageReady || s.client->persistencePending() ||
        s.root.sequence < s.checkpoint_attempt_sequence || s.root.sequence - s.checkpoint_attempt_sequence < interval ||
        s.workspace_owner.holder() || s.response || announcement_pending.load(std::memory_order_acquire) || downloadActive() || publicationActive() || draftSaveActive()) return false;
    s.checkpoint.reset(new (std::nothrow) SdCheckpointRotation<Digest>(s.volume));
    if (!s.checkpoint)
    {
        next_step.store(millis() + 1000);
        return true;
    }
    if (!s.workspace_owner.acquire(s.checkpoint.get()))
    {
        s.checkpoint.reset();
        return false;
    }
    // Sorting reuses the 8 KiB encoding buffer; the smaller signature scratch
    // cannot hold a run. Import subsequently reuses it for value comparison.
    if (!s.checkpoint->begin(s.roots[0], s.roots[1], s.root_copy, s.frame, kFrameCapacity, s.encoded, kEncodingCapacity, interval))
    {
        s.workspace_owner.release(s.checkpoint.get());
        s.checkpoint.reset();
        s.checkpoint_recovery_required = true;
        fail("Storage maintenance could not validate index roots");
        return true;
    }
    s.checkpoint_attempt_sequence = s.root.sequence;
    return true;
}

bool resumeWaitingDownload(Session& s)
{
    if (s.download_recovery_complete) return false;
    gc::storage::DownloadRecoveryRequest recovered;
    gc::protocol::SummaryView summary;
    const auto result = s.download_store->readWaitingDownload(s.local, recovered, summary);
    if (result == DownloadRecoveryRead::Busy) return false;
    s.download_restore_pending = result == DownloadRecoveryRead::Pending;
    if (s.download_restore_pending) return true;
    if (result == DownloadRecoveryRead::Unavailable && !s.download_store->needsRecovery())
    {
        next_step.store(millis() + 1000);
        return true;
    }
    if (result == DownloadRecoveryRead::End)
    {
        s.download_recovery_complete = true;
        return false;
    }
    if (result != DownloadRecoveryRead::Ready)
    {
        fail(result == DownloadRecoveryRead::WorkspaceTooSmall ? "Insufficient download recovery workspace"
             : result == DownloadRecoveryRead::IoError         ? "Cannot read waiting download"
             : result == DownloadRecoveryRead::VolumeChanged   ? "Download storage volume changed"
                                                               : "Download preview missing - recovery needs attention");
        return true;
    }
    // The name is the only borrowed preview field. Preserve this small text
    // while releasing the shared frame for the port's request validation.
    std::array<char, gc::kMaxNameBytes> name{};
    std::memcpy(name.data(), summary.name.data(), summary.name.size());
    summary.name = {name.data(), summary.name.size()};
    s.download_store->releaseRead();
    gc::Destination remote;
    gc::RequestId request;
    std::memcpy(remote.bytes.data(), recovered.key.data() + 16, 16);
    std::memcpy(request.bytes.data(), recovered.key.data() + 32, 16);
    s.download_port.reset(new (std::nothrow) SdDownloadPort<Digest>(*s.download_store, s.crypto, s.local,
                                                                    recovered.identity, recovered.task, recovered.created));
    if (s.download_port) s.download.reset(new (std::nothrow) gc::DownloadClient(*s.download_port, s.crypto));
    if (!s.download)
    {
        s.download_port.reset();
        next_step.store(millis() + 1000);
        return true;
    }
    if (!s.download->resume(remote, request, summary, recovered.identity.generation, s.download_port->resumeWaiting(remote, request)))
    {
        fail("Cannot resume download request");
        return true;
    }
    s.download_started = now(nullptr).monotonic_ms;
    // Allow expiry/retry of the previous boot's attempt before reply timeout.
    s.download_wait_ms = 2 * gc::QueryClient::kReplyTimeoutMs + 5000;
    s.download_scratch = summary.signed_bytes + 64;
    ++epoch;
    return true;
}

bool advanceDownloadStart(Session& s)
{
    if (!s.download_start) return false;
    // Dispose of the previous completed port before taking the new read lease.
    s.download.reset();
    s.download_port.reset();
    auto& job = *s.download_start;
    uint64_t generation = 0;
    const auto result = s.download_store->readNextGeneration(job.summary.id, generation);
    if (result == DownloadRecoveryRead::Busy) return false;
    if (result == DownloadRecoveryRead::Pending) return true;
    if (result == DownloadRecoveryRead::Unavailable && !s.download_store->needsRecovery())
    {
        next_step.store(millis() + 1000);
        return true;
    }
    if (result != DownloadRecoveryRead::Ready || !generation)
    {
        s.download_store->releaseRead();
        s.download_start.reset();
        s.download_start_error = result == DownloadRecoveryRead::Ready               ? "Download generation limit reached"
                                 : result == DownloadRecoveryRead::WorkspaceTooSmall ? "Insufficient download workspace"
                                 : result == DownloadRecoveryRead::IoError           ? "Cannot read download metadata"
                                 : result == DownloadRecoveryRead::VolumeChanged     ? "Download storage volume changed"
                                                                                     : "Download metadata needs recovery";
        ++epoch;
        return true;
    }
    std::array<uint8_t, 16> task;
    gc::RequestId request;
    randomId(nullptr, task.data());
    randomId(nullptr, request.bytes.data());
    s.download_port.reset(new (std::nothrow) SdDownloadPort<Digest>(*s.download_store, s.crypto, s.local,
                                                                    {job.summary.id, job.summary.hash, generation}, task, now(nullptr)));
    if (s.download_port) s.download.reset(new (std::nothrow) gc::DownloadClient(*s.download_port, s.crypto));
    if (!s.download)
    {
        s.download_port.reset();
        s.download_store->releaseRead();
        next_step.store(millis() + 1000);
        return true;
    }
    const auto scratch = job.summary.signed_bytes + 64;
    const bool begun = s.download->begin(job.remote, request, job.summary, generation);
    s.download_start.reset();
    if (!begun)
    {
        s.download.reset();
        s.download_port.reset();
        s.download_start_error = "Download could not be started";
        ++epoch;
        return true;
    }
    s.download_started = now(nullptr).monotonic_ms;
    s.download_wait_ms = gc::QueryClient::kReplyTimeoutMs;
    s.download_scratch = scratch;
    if (s.saved) s.saved->reset();
    ++epoch;
    return true;
}

void announcementReceived(const chat::lxmf::GeocachingAnnouncementView& message, void*)
{
    if (!wanted.load() || announcement_pending.load(std::memory_order_acquire) || message.discovery_destination.size != 16 ||
        message.delivery_destination.size != 16 || message.public_key.size != 64 || message.app_data.size > 128 ||
        !message.discovery_destination.data || !message.delivery_destination.data || !message.public_key.data || !message.app_data.data) return;
    auto& out = pending_announcement;
    std::memcpy(out.discovery.bytes.data(), message.discovery_destination.data, 16);
    std::memcpy(out.delivery.bytes.data(), message.delivery_destination.data, 16);
    std::memcpy(out.key.data(), message.public_key.data, 64);
    std::memcpy(out.data.data(), message.app_data.data, message.app_data.size);
    out.size = message.app_data.size;
    announcement_pending.store(true, std::memory_order_release);
    next_step.store(0);
}
bool receiveResponse(const chat::lxmf::CustomDeliveryView& message, uint8_t** owned = nullptr)
{
    if (!session || !session->port || (!wanted.load() && !downloadActive() && !publicationActive()) || message.source.size != 16 ||
        message.destination.size != 16 || !message.data.data || message.data.size > 8192) return false;
    if (std::memcmp(message.destination.data, session->local.bytes.data(), 16)) return false;
    gc::Destination source;
    std::memcpy(source.bytes.data(), message.source.data, 16);
    gc::protocol::CmpReader reader({message.data.data, message.data.size});
    size_t fields = 0;
    uint64_t value = 0;
    gc::ByteView id;
    if (!reader.array(fields, 6) || fields != 6 || !reader.unsignedInteger(value) || value != 1 ||
        !reader.unsignedInteger(value) || value != 1 || !reader.unsignedInteger(value) || value > 3 ||
        !reader.binary(id, 16) || id.size != 16) return false;
    gc::RequestId request;
    std::memcpy(request.bytes.data(), id.data, 16);
    const bool live_query = session->client && session->client->expectsResponse(source, request);
    if (!live_query && session->port->accepted(source, request, {message.data.data, message.data.size})) return true;
    if (value == 1 && (!session->publication || !session->publication->attempt ||
                       session->publication->attempt->phase() != gc::PublishAttemptPhase::Waiting || message.data.size > 512)) return false;
    if (value == 3 && (!session->download || session->download->phase() != gc::DownloadPhase::Waiting ||
                       message.data.size > session->download_scratch)) return false;
    if (value != 3 && message.data.size > 2048) return false;
    if (session->response) return false;
    auto* bytes = owned ? *owned : static_cast<uint8_t*>(mem::allocatePreferred("geocaching.rx", message.data.size, false));
    if (!bytes) return false;
    if (owned) *owned = nullptr;
    else std::memcpy(bytes, message.data.data, message.data.size);
    session->response = bytes;
    session->response_size = message.data.size;
    session->response_source = source;
    session->response_id = request;
    session->response_operation = static_cast<uint8_t>(value);
    ++replies_queued;
    next_step.store(0);
    // The sender can retry; only the exact committed response is acknowledged.
    return false;
}

bool responseReceived(const chat::lxmf::CustomDeliveryView& message, void*)
{
    ++replies_seen;
    if (!message.source.data || message.source.size != 16 || !message.destination.data || message.destination.size != 16 ||
        !message.data.data || !message.data.size || message.data.size > 8192) return false;
    Guard guard;
    if (guard.locked) return receiveResponse(message);
    ++replies_busy;
    if (!active.load() || pending_reply.load(std::memory_order_acquire)) return false;
    auto reply = std::unique_ptr<PendingReply>(new (std::nothrow) PendingReply);
    if (!reply) return false;
    reply->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.rx", message.data.size, false));
    if (!reply->bytes) return false;
    std::memcpy(reply->source.bytes.data(), message.source.data, 16);
    std::memcpy(reply->destination.bytes.data(), message.destination.data, 16);
    std::memcpy(reply->bytes, message.data.data, message.data.size);
    reply->size = message.data.size;
    pending_reply.store(reply.release(), std::memory_order_release);
    next_step.store(0);
    return false; // Queued in RAM only; durable acceptance still controls ACKs.
}

void drainReply(Session& s)
{
    if (s.response || s.phase != Phase::Ready || !s.port) return;
    std::unique_ptr<PendingReply> reply(pending_reply.exchange(nullptr, std::memory_order_acq_rel));
    if (!reply) return;
    receiveResponse({{reply->source.bytes.data(), 16}, {reply->destination.bytes.data(), 16}, {}, {reply->bytes, reply->size}}, &reply->bytes);
}

bool processResponse(Session& s)
{
    if (!s.response || s.client->persistencePending() || s.dispatch_store->busy()) return false;
    ++replies_processed;
    if (s.response_operation == 1 && s.publication && s.publication->attempt)
    {
        s.publication->attempt->accept(s.response_source, {s.response, s.response_size});
        ++epoch;
    }
    else if (s.response_operation == 3 && s.download)
    {
        auto* scratch = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.verify", s.download_scratch, false));
        if (!scratch) return false;
        const auto before = s.download->phase();
        s.download->accept(s.response_source, {s.response, s.response_size}, scratch, s.download_scratch);
        if (before != s.download->phase()) ++epoch;
        heap_caps_free(scratch);
    }
    else
    {
        const bool accepted = s.client->accept(s.response_source, {s.response, s.response_size});
        if (accepted) ++replies_accepted;
        Serial.printf("[Geocaching] reply operation=%u result=%s query=%u bytes=%u\n",
                      s.response_operation, accepted ? "committed" : s.client->persistencePending() ? "persisting"
                                                                                                    : "ignored",
                      static_cast<unsigned>(s.client->phase()), static_cast<unsigned>(s.response_size));
    }
    heap_caps_free(s.response);
    s.response = nullptr;
    s.response_size = 0;
    return true;
}

void startRecovery()
{
    auto& s = *session;
    if (!s.ensureBuffers(false))
    {
        fail("Insufficient storage workspace");
        return;
    }
    s.recovery.reset(new (std::nothrow) SdIndexRepair<Digest>(s.volume, s.roots[0], s.roots[1],
                                                              s.frame, kFrameCapacity, s.encoded, kEncodingCapacity,
                                                              s.mutations.data(), s.mutations.size()));
    if (!s.recovery)
    {
        fail("Insufficient memory");
        return;
    }
    s.phase = Phase::Recover;
    s.status = "Restoring geocaching tasks...";
    ++epoch;
}

enum class PublicationRestore : uint8_t
{
    None,
    Pending,
    Restored,
    Invalid
};
PublicationRestore restorePublication(Session& s, const gc::GeocacheId* cache = nullptr,
                                      const gc::RevisionHash* hash = nullptr, const gc::Destination* remote = nullptr)
{
    const bool background = !cache && !hash && !remote;
    if (background && s.publication_recovery_complete) return PublicationRestore::None;
    gc::storage::PublicationRecoveryFilter filter;
    filter.local = s.local;
    filter.has_cache = cache != nullptr;
    filter.has_hash = hash != nullptr;
    filter.has_remote = remote != nullptr;
    if (cache) filter.cache = *cache;
    if (hash) filter.hash = *hash;
    if (remote) filter.remote = *remote;
    gc::storage::PublicationRecoveryView selected;
    const auto result = s.store->readPublicationRecovery(filter, selected);
    s.publication_restore_pending = result == DraftReadResult::Pending;
    s.publication_restore_background = background;
    if (result == DraftReadResult::Pending || result == DraftReadResult::Busy) return PublicationRestore::Pending;
    if (result == DraftReadResult::Unavailable && !s.needsRecovery())
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    // All exits after Ready release the borrowed request/result frame. Resume
    // copies its small identity/state fields and never retains the payload.
    struct ReadLease
    {
        PublicationStore& store;
        ~ReadLease() { store.releaseDraftRead(); }
    } lease{*s.store};
    if (result == DraftReadResult::NotFound)
    {
        if (background) s.publication_recovery_complete = true;
        return PublicationRestore::None;
    }
    if (result != DraftReadResult::Ready || !selected.request.data || selected.request.size > gc::kMaxApplicationBytes)
        return PublicationRestore::Invalid;
    gc::RequestId request;
    std::memcpy(request.bytes.data(), selected.key.data() + 32, 16);
    auto job = std::unique_ptr<Session::Publication>(new (std::nothrow) Session::Publication);
    if (!job)
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    std::memcpy(job->remote.bytes.data(), selected.key.data() + 16, 16);
    job->port.reset(new (std::nothrow) SdPublishPort(*s.store, s.crypto, s.local, selected.cache, selected.hash, selected.task, selected.created));
    if (job->port) job->attempt.reset(new (std::nothrow) gc::PublishAttempt(*job->port, s.crypto));
    if (!job->attempt)
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    if (!job->port->attachRestoredRequest(job->remote, request)) return PublicationRestore::Invalid;
    const bool reuse = s.publication && s.publication->bytes;
    const size_t capacity = reuse ? s.publication->size + 26 : selected.request.size + 26;
    auto* scratch = reuse ? s.publication->bytes + s.publication->size
                          : static_cast<uint8_t*>(mem::allocatePreferred("geocaching.publish.restore", capacity, false));
    if (!scratch)
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    const bool restored = job->attempt->resume(job->remote, request, selected.request, scratch, capacity,
                                               selected.cache, selected.hash, selected.response);
    if (!reuse) heap_caps_free(scratch);
    if (!restored) return PublicationRestore::Invalid;
    job->started = now(nullptr).monotonic_ms;
    job->wait_ms = 2 * gc::QueryClient::kReplyTimeoutMs + 5000;
    s.publication = std::move(job);
    s.draft_save.reset();
    ++epoch;
    return PublicationRestore::Restored;
}

void advanceDraftPublication(Session& s)
{
    auto& job = *s.publication;
    using Stage = Session::Publication::DraftStage;
    const auto stop = [&](const char* reason)
    {
        job.error = reason;
        job.draft_stage = Stage::None;
        job.issue.reset();
        job.author_port.reset();
        heap_caps_free(job.bytes);
        job.bytes = nullptr;
        heap_caps_free(job.unsigned_bytes);
        job.unsigned_bytes = nullptr;
        heap_caps_free(job.draft_bytes);
        job.draft_bytes = nullptr;
        s.store->releaseDraftRead();
        ++epoch;
    };
    if (job.draft_stage == Stage::Binding)
    {
        const auto result = s.store->stepCommit();
        if (result == JournalWriteResult::InProgress || result == JournalWriteResult::Busy) return;
        if (result != JournalWriteResult::Verified)
        {
            stop("Author binding could not be saved");
            return;
        }
        heap_caps_free(job.unsigned_bytes);
        job.unsigned_bytes = nullptr;
        job.draft_stage = Stage::Encode;
        return;
    }
    if (job.draft_stage == Stage::Sign)
    {
        job.issue->advance();
        if (job.issue->phase() == gc::AuthorIssuePhase::Failed)
        {
            stop("Version reservation or signing failed");
            return;
        }
        if (job.issue->phase() != gc::AuthorIssuePhase::Signed) return;
        job.size = job.issue->signedRecord().size;
        job.issue.reset();
        job.author_port.reset();
        heap_caps_free(job.unsigned_bytes);
        job.unsigned_bytes = nullptr;
        job.draft_stage = Stage::None;
        ++epoch;
        return;
    }
    gc::ByteView value;
    gc::storage::DraftView draft;
    const gc::ByteView key{job.draft_id.data(), job.draft_id.size()};
    if (!job.draft_bytes)
    {
        const auto result = s.store->readDraft(key, value);
        if (result == DraftReadResult::Pending || result == DraftReadResult::Busy) return;
        if (result != DraftReadResult::Ready)
        {
            stop("Draft could not be read");
            return;
        }
        // The store lends its shared frame only until releaseDraftRead. Keep
        // just this draft while encoding, then release it before reservation.
        job.draft_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.publish-draft", value.size, false));
        if (!job.draft_bytes)
        {
            stop("Insufficient draft workspace");
            return;
        }
        job.draft_size = value.size;
        std::memcpy(job.draft_bytes, value.data, value.size);
        s.store->releaseDraftRead();
    }
    value = {job.draft_bytes, job.draft_size};
    if (!gc::storage::decodeDraft(key, value, draft) || draft.generation != job.draft_generation)
    {
        stop("Draft changed; review before publishing");
        return;
    }
    if (job.draft_stage == Stage::Bind)
    {
        if (draft.author.size)
        {
            if (draft.author.size != job.author.size() || std::memcmp(draft.author.data, job.author.data(), job.author.size()))
                stop("Draft belongs to another author");
            else
                job.draft_stage = Stage::Encode;
            return;
        }
        if (draft.generation == UINT64_MAX)
        {
            stop("Draft generation limit reached");
            return;
        }
        job.unsigned_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.author", value.size + 80, false));
        if (!job.unsigned_bytes)
        {
            stop("Insufficient author workspace");
            return;
        }
        draft.author = {job.author.data(), job.author.size()};
        ++draft.generation;
        if (!gc::storage::encodeDraft(key, draft, job.unsigned_bytes, value.size + 80, job.unsigned_size))
        {
            stop("Cannot encode author binding");
            return;
        }
        const auto result = s.store->saveDraft(key, {job.unsigned_bytes, job.unsigned_size}, job.draft_generation);
        if (result == JournalWriteResult::Busy)
        {
            heap_caps_free(job.unsigned_bytes);
            job.unsigned_bytes = nullptr;
            return;
        }
        if (result != JournalWriteResult::InProgress && result != JournalWriteResult::Verified)
        {
            stop("Author binding rejected");
            return;
        }
        ++job.draft_generation;
        heap_caps_free(job.draft_bytes);
        job.draft_bytes = nullptr;
        if (result == JournalWriteResult::Verified)
        {
            heap_caps_free(job.unsigned_bytes);
            job.unsigned_bytes = nullptr;
        }
        job.draft_stage = result == JournalWriteResult::InProgress ? Stage::Binding : Stage::Encode;
        return;
    }
    if (draft.author.size != 64 || std::memcmp(draft.author.data, job.author.data(), 64) || !draft.has_coordinates)
    {
        stop("Draft is not ready for first publication");
        return;
    }
    gc::RecordView record;
    record.author_public_key = {job.author.data(), job.author.size()};
    // draft_id is already a durable CSPRNG nonce. Reuse it for this cache's
    // creation nonce so reopening or retrying never creates a different ID.
    record.creation_nonce = key;
    record.revision = 1;
    record.state = static_cast<gc::CacheState>(draft.state);
    record.latitude_e7 = draft.latitude_e7;
    record.longitude_e7 = draft.longitude_e7;
    record.name = draft.name;
    record.description = draft.description;
    record.hint = draft.hint;
    record.difficulty_x2 = draft.difficulty_x2;
    record.terrain_x2 = draft.terrain_x2;
    record.container_size = static_cast<gc::ContainerSize>(draft.container_size);
    record.created_at = record.updated_at = job.issued.utc_seconds;
    const size_t capacity = draft.name.size() + draft.description.size() + draft.hint.size() + 200;
    gc::RevisionHash hash;
    if (!job.unsigned_bytes)
    {
        job.unsigned_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.record", capacity, false));
        job.size = capacity + 70;
        job.bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.sign", 2 * job.size + 26, false));
        if (!job.unsigned_bytes || !job.bytes || !gc::protocol::encodeGeocacheRecord(record, job.unsigned_bytes, capacity, job.unsigned_size) ||
            gc::protocol::deriveGeocacheHashes({job.unsigned_bytes, job.unsigned_size}, s.crypto, job.bytes, job.size, job.cache, hash) != gc::protocol::VerificationResult::Valid)
        {
            stop("Cannot prepare signed record");
            return;
        }
    }
    const auto history_result = s.store->readPublicationHistory(key, job.draft_generation, job.cache,
                                                                {job.author.data(), job.author.size()}, job.history);
    if (history_result == DraftReadResult::Pending || history_result == DraftReadResult::Busy) return;
    if (history_result != DraftReadResult::Ready)
    {
        stop("Author history could not be read");
        return;
    }
    const auto& history = job.history;
    if (history.latest_revision)
    {
        record.created_at = history.created_at;
        record.revision = history.latest_revision;
        record.updated_at = history.latest_time.utc_seconds;
        if (record.revision > 1)
            record.previous_hash = {history.previous_hash.bytes.data(), history.previous_hash.bytes.size()};
        if (!gc::protocol::encodeGeocacheRecord(record, job.unsigned_bytes, capacity, job.unsigned_size) ||
            gc::protocol::deriveGeocacheHashes({job.unsigned_bytes, job.unsigned_size}, s.crypto, job.bytes, job.size, job.cache, hash) != gc::protocol::VerificationResult::Valid)
        {
            stop("Cannot reconstruct latest version");
            return;
        }
        if (hash.bytes == history.latest_hash.bytes) job.issued = history.latest_time;
        else
        {
            if (history.confirmed_revision != history.latest_revision)
            {
                stop("Previous version unconfirmed; resolve it first");
                return;
            }
            if (history.latest_revision == UINT32_MAX || job.issued.utc_seconds < history.latest_time.utc_seconds)
            {
                stop("Version limit or clock regression");
                return;
            }
            record.revision = history.latest_revision + 1;
            record.previous_hash = {history.latest_hash.bytes.data(), history.latest_hash.bytes.size()};
            record.updated_at = job.issued.utc_seconds;
            if (!gc::protocol::encodeGeocacheRecord(record, job.unsigned_bytes, capacity, job.unsigned_size))
            {
                stop("Cannot encode successor version");
                return;
            }
        }
    }
    if (job.expected_revision && record.revision != job.expected_revision)
    {
        stop("Version changed; review publication again");
        return;
    }
    heap_caps_free(job.draft_bytes);
    job.draft_bytes = nullptr;
    job.author_port.reset(new (std::nothrow) DeviceAuthorIssuePort(*router, *s.store, s.crypto, job.issued, key, job.draft_generation));
    if (job.author_port) job.issue.reset(new (std::nothrow) gc::AuthorIssue(*job.author_port));
    if (!job.issue || !job.issue->begin({job.unsigned_bytes, job.unsigned_size}, job.bytes, job.size))
    {
        stop("Cannot start record signing");
        return;
    }
    job.draft_stage = Stage::Sign;
}

bool publicationDraftReady(const std::array<uint8_t, 16>& id, uint64_t generation, std::array<uint8_t, 64>& author,
                           uint32_t* from = nullptr, uint32_t* to = nullptr)
{
    if (!session) return false;
    if (!draftCatalogReady(*session))
    {
        session->draft_catalog_wanted = true;
        next_step.store(0);
        return false;
    }
    const gc::storage::DraftCatalogEntry* draft = nullptr;
    for (size_t i = 0; i < session->draft_catalog->page.count; ++i)
    {
        const auto& candidate = session->draft_catalog->page.rows[i];
        if (candidate.id == id && candidate.generation == generation)
        {
            draft = &candidate;
            break;
        }
    }
    if (!draft) return false;
    gc::Destination remote;
    const auto utc = std::time(nullptr);
    const bool ready = session && session->phase == Phase::Ready && session->port && session->store &&
                       !session->needsRecovery() && !session->store->commitPending() && !session->client->persistencePending() &&
                       !downloadActive() && !publicationActive() && !draftSaveActive() && utc >= 946684800 &&
                       static_cast<uint64_t>(utc) <= 253402300799ULL && session->port->pageSource(remote) &&
                       draft->has_coordinates &&
                       gc::protocol::validRecordText(draft->name.data(), false, true) && router->getGeocachingAuthorKey(author.data()) &&
                       (!draft->has_author || draft->author == author);
    if (!ready) return false;
    const auto& history = draft->publication;
    if (history.local_changes && (history.latest_revision == UINT32_MAX || history.confirmed_revision != history.latest_revision)) return false;
    if (from) *from = history.latest_revision;
    if (to) *to = history.latest_revision ? history.latest_revision + (history.local_changes ? 1 : 0) : 1;
    return true;
}

class Facade final : public ::ui::geocaching::Source
{
  public:
    void activate(bool open) override
    {
        wanted.store(open);
        next_step.store(0);
    }
    void snapshot(::ui::geocaching::Section section, ::ui::geocaching::Snapshot& out) override
    {
        out = {};
        Guard guard;
        if (!guard.locked)
        {
            out.busy = true;
            std::snprintf(out.status.data(), out.status.size(), "Updating...");
            return;
        }
        if (section == ::ui::geocaching::Section::Published && session && session->store && !session->needsRecovery())
        {
            session->draft_catalog_wanted = true;
            const auto* catalog = session->draft_catalog.get();
            out.count = catalog ? catalog->total : 0;
            out.generation = catalog ? catalog->generation : 0;
            out.can_create = !session->store->commitPending() && !downloadActive() && !publicationActive() && !draftSaveActive() &&
                             storage::sd_card_ready() && !storage::sd_external_block_owner_active();
            std::snprintf(out.status.data(), out.status.size(), "%s", catalog && catalog->failed ? "Draft list could not be read" : !draftCatalogReady(*session) ? "Loading drafts and publication status..."
                                                                                                                                : out.count                      ? "Drafts and saved publication status"
                                                                                                                                                                 : "No local drafts");
            if (!draftCatalogReady(*session) && (!catalog || !catalog->failed)) next_step.store(0);
        }
        else if (section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->needsRecovery())
            session->saved->snapshot(out);
        else if (session && session->source && session->phase == Phase::Ready) session->source->snapshot(section, out);
        else
        {
            std::snprintf(out.status.data(), out.status.size(), "%s", session ? session->status : "Starting Geocaching...");
            out.can_refresh = session && session->phase == Phase::Failed;
        }
        if (session && session->recovery_attention && section == ::ui::geocaching::Section::Downloaded)
            std::snprintf(out.status.data(), out.status.size(), "Some saved GPX files or history need attention");
        if (session && session->saved && session->saved->error() && section == ::ui::geocaching::Section::Discover)
            std::snprintf(out.status.data(), out.status.size(), "%s", session->saved->error());
        if (session && session->notice) std::snprintf(out.status.data(), out.status.size(), "%s", session->notice);
        if (downloadActive())
        {
            out.can_refresh = false;
            out.has_more = false;
            const auto phase = session->download ? session->download->phase() : gc::DownloadPhase::Submitting;
            std::snprintf(out.status.data(), out.status.size(), "%s", session->download_start ? "Preparing download..." : phase == gc::DownloadPhase::Waiting  ? "Downloading cache..."
                                                                                                                      : phase == gc::DownloadPhase::Cancelling ? "Cancelling download..."
                                                                                                                                                               : "Saving and verifying GPX...");
        }
        else if (session && session->download_start_error)
            std::snprintf(out.status.data(), out.status.size(), "%s", session->download_start_error);
        else if (session && session->download && session->download->phase() == gc::DownloadPhase::Failed)
            std::snprintf(out.status.data(), out.status.size(), "Download could not be saved");
        else if (session && session->download_port && session->download_port->historyPending())
            std::snprintf(out.status.data(), out.status.size(), "Saved; history retention pending");
        if (session && session->publication && (publicationActive() || section == ::ui::geocaching::Section::Published))
        {
            const auto& job = *session->publication;
            const auto phase = job.attempt ? job.attempt->phase() : gc::PublishAttemptPhase::Failed;
            const char* status = job.bytes ? "Preparing publication..." : phase == gc::PublishAttemptPhase::Confirmed                                                ? "Directory accepted publication"
                                                                      : phase == gc::PublishAttemptPhase::Waiting                                                    ? "Waiting for directory confirmation..."
                                                                      : phase == gc::PublishAttemptPhase::Submitting || phase == gc::PublishAttemptPhase::Committing ? "Saving publication state..."
                                                                      : phase == gc::PublishAttemptPhase::Cancelling                                                 ? "Stopping publication retries..."
                                                                      : phase == gc::PublishAttemptPhase::Cancelled                                                  ? "Stopped; publication result unconfirmed"
                                                                                                                                                                     : "Publication could not complete";
            if (job.error) status = job.error;
            else if (job.draft_stage != Session::Publication::DraftStage::None) status = "Preparing publication...";
            std::snprintf(out.status.data(), out.status.size(), "%s", status);
            if (publicationActive())
            {
                out.can_refresh = false;
                out.has_more = false;
            }
        }
        if (session && session->draft_save && (draftSaveActive() || section == ::ui::geocaching::Section::Published))
        {
            const auto& job = *session->draft_save;
            std::snprintf(out.status.data(), out.status.size(), "%s", !job.done ? "Saving local draft..." : job.saved ? "Draft saved locally"
                                                                                                                      : "Draft save failed; changes not saved");
            if (!job.done)
            {
                out.can_refresh = false;
                out.has_more = false;
            }
        }
        if (session && session->workspace_unavailable)
            std::snprintf(out.status.data(), out.status.size(), "Waiting for storage workspace...");
        if (session && session->phase == Phase::Failed)
        {
            std::snprintf(out.status.data(), out.status.size(), "%s", session->status);
            out.can_refresh = true;
            out.can_create = out.has_more = false;
        }
        if (session && session->load_more_pending) out.has_more = false;
        out.generation ^= epoch << 32;
    }
    void requestWindow(::ui::geocaching::Section section, size_t offset, size_t count) override
    {
        Guard guard;
        if (!guard.locked || !session) return;
        session->draft_catalog_wanted = section == ::ui::geocaching::Section::Published;
        if (session->saved && count && count <= 4)
        {
            auto& saved = *session->saved;
            const auto before = saved.generation();
            if (section == ::ui::geocaching::Section::Downloaded) saved.requestWindow(offset, count);
            else if (section == ::ui::geocaching::Section::Discover && session->port)
            {
                gc::protocol::QueryPageView page;
                const auto available = session->port->page(page) && offset < page.count ? std::min(count, page.count - offset) : 0;
                if (saved.requestPreview(session->port->generation(), offset, available))
                    for (size_t i = 0; i < available; ++i)
                    {
                        gc::protocol::SummaryView row;
                        if (session->port->summary(offset + i, row)) saved.previewRow(i, row.id.bytes, row.hash.bytes);
                    }
            }
            if (before != saved.generation())
            {
                ++epoch;
                next_step.store(0);
            }
        }
        if (!session->draft_catalog_wanted || !count || count > 4) return;
        if (session->draft_catalog && session->draft_catalog->requested_offset != offset)
        {
            auto& catalog = *session->draft_catalog;
            catalog.requested_offset = offset;
            catalog.failed = catalog.ready = false;
            if (catalog.reading) session->draft_io_reset = true;
            ++catalog.generation;
        }
        next_step.store(0);
    }
    bool item(::ui::geocaching::Section section, size_t index, uint64_t generation, ::ui::geocaching::Item& out) override
    {
        Guard guard;
        if (guard.locked && section == ::ui::geocaching::Section::Published && session && session->store && !session->needsRecovery())
        {
            out = {};
            if (!draftCatalogReady(*session)) return false;
            const auto& catalog = *session->draft_catalog;
            if ((generation ^ (epoch << 32)) != catalog.generation || index < catalog.page.offset || index - catalog.page.offset >= catalog.page.count) return false;
            const auto& draft = catalog.page.rows[index - catalog.page.offset];
            out.is_draft = true;
            out.edit_generation = draft.generation;
            std::memcpy(out.id.data(), draft.id.data(), 16);
            if (!draft.name[0]) std::snprintf(out.name.data(), out.name.size(), "Untitled draft");
            else out.name = draft.name;
            out.latitude_e7 = draft.latitude_e7;
            out.longitude_e7 = draft.longitude_e7;
            std::snprintf(out.detail.data(), out.detail.size(), "Local draft - not published\n%s\n%s", draft.has_coordinates ? "Location set" : "Location not set", draft.has_author ? "Author selected" : "Author not selected");
            const auto& publication = draft.publication;
            out.publication_revision = publication.latest_revision;
            out.publication_confirmed = publication.latest_revision && publication.confirmed_revision == publication.latest_revision;
            if (publication.latest_revision)
            {
                std::snprintf(out.detail.data(), out.detail.size(), publication.confirmed_revision ? "v%lu %s%s\nLast directory-confirmed version: %lu" : "v%lu %s%s\nNo directory confirmation recorded",
                              static_cast<unsigned long>(publication.latest_revision), out.publication_confirmed ? "accepted by directory" : publication.pending ? "awaiting confirmation"
                                                                                                                                                                 : "stopped; result unconfirmed",
                              publication.local_changes ? "; local edits" : "",
                              static_cast<unsigned long>(publication.confirmed_revision));
            }
            return true;
        }
        if (guard.locked && section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->needsRecovery())
            return session->saved->item(index, generation ^ (epoch << 32), out);
        if (!guard.locked || !session || !session->source || !session->source->item(section, index, generation ^ (epoch << 32), out)) return false;
        out.downloaded = session->saved && session->saved->contains(out.id, out.revision_hash);
        out.can_download = !out.downloaded && !downloadActive() && !publicationActive() && !draftSaveActive() && !session->store->commitPending() &&
                           session->saved && session->saved->checked(out.id, out.revision_hash) &&
                           !session->needsRecovery() && storage::sd_card_ready() &&
                           (session->client->phase() == gc::QueryClientPhase::PageReady || session->client->phase() == gc::QueryClientPhase::Failed);
        if (out.downloaded)
        {
            if (auto* suffix = std::strstr(out.detail.data(), "Directory preview - not yet downloaded"))
                std::snprintf(suffix, out.detail.size() - static_cast<size_t>(suffix - out.detail.data()), "Saved GPX on SD card");
        }
        return true;
    }
    void refresh(::ui::geocaching::Section section) override
    {
        Guard guard;
        if (!guard.locked || downloadActive() || publicationActive() || draftSaveActive()) return;
        if (section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->needsRecovery()) session->saved->reset();
        else if (session && session->phase == Phase::Failed) restart.store(true);
        else if (session && session->source) session->source->refresh(section);
        next_step.store(0);
    }
    bool loadMore() override
    {
        Guard guard;
        if (!guard.locked || downloadActive() || publicationActive() || draftSaveActive() || !session || !session->source || session->load_more_pending) return false;
        if (session->checkpoint)
        {
            if (!session->client || !session->client->hasMore() || !session->port || session->needsRecovery()) return false;
            session->load_more_pending = true;
            session->load_more_generation = session->port->generation();
            next_step.store(0);
            ++epoch;
            return true;
        }
        const bool begun = session->source->loadMore();
        if (begun) next_step.store(0);
        return begun;
    }
    void open(const ::ui::geocaching::Item&, uint64_t) override {}
    bool publicationAuthor(const std::array<uint8_t, 16>& id, uint64_t generation, std::array<uint8_t, 64>& author, uint32_t* from, uint32_t* to) override
    {
        Guard guard;
        return guard.locked && publicationDraftReady(id, generation, author, from, to);
    }
    bool publishDraft(const std::array<uint8_t, 16>& id, uint64_t generation, const std::array<uint8_t, 64>& expected_author, uint32_t expected_revision) override
    {
        Guard guard;
        std::array<uint8_t, 64> author;
        uint32_t revision = 0;
        if (!guard.locked || !publicationDraftReady(id, generation, author, nullptr, &revision) || author != expected_author || revision != expected_revision) return false;
        auto job = std::unique_ptr<Session::Publication>(new (std::nothrow) Session::Publication);
        if (!job || !session->port->pageSource(job->remote)) return false;
        job->draft_id = id;
        job->draft_generation = generation;
        job->author = author;
        job->expected_revision = expected_revision;
        job->issued = now(nullptr);
        job->issued.has_utc = true;
        job->issued.utc_seconds = static_cast<uint64_t>(std::time(nullptr));
        job->draft_stage = Session::Publication::DraftStage::Bind;
        session->draft_save.reset();
        session->publication = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    ::ui::geocaching::DraftReadStatus readDraft(const std::array<uint8_t, 16>& id, void (*sink)(const ::ui::geocaching::DraftInput&, void*), void* context) override
    {
        Guard guard;
        using Status = ::ui::geocaching::DraftReadStatus;
        if (!guard.locked) return Status::Pending;
        if (!sink || !session || !session->store || session->needsRecovery()) return Status::Failed;
        if (cancel_draft_read.exchange(false))
        {
            session->draft_read.reset();
            session->draft_io_reset = true;
        }
        if (!session->draft_read || session->draft_read->id != id)
        {
            session->draft_io_reset = true;
            session->draft_read.reset(new (std::nothrow) Session::DraftRead);
            if (!session->draft_read) return Status::Failed;
            session->draft_read->id = id;
            next_step.store(0);
            return Status::Pending;
        }
        if (session->draft_read->status == Status::Pending) return Status::Pending;
        if (session->draft_read->status == Status::Failed)
        {
            session->draft_read.reset();
            return Status::Failed;
        }
        gc::storage::DraftView draft;
        const auto& result = *session->draft_read;
        if (!gc::storage::decodeDraft({id.data(), id.size()}, {result.bytes, result.size}, draft))
        {
            session->draft_read.reset();
            return Status::Failed;
        }
        ::ui::geocaching::DraftInput out;
        out.id = id;
        out.generation = draft.generation;
        out.name = draft.name;
        out.description = draft.description;
        out.hint = draft.hint;
        out.latitude_e7 = draft.latitude_e7;
        out.longitude_e7 = draft.longitude_e7;
        out.has_coordinates = draft.has_coordinates;
        out.state = draft.state;
        out.difficulty_x2 = draft.difficulty_x2;
        out.terrain_x2 = draft.terrain_x2;
        out.container_size = draft.container_size;
        sink(out, context);
        session->draft_read.reset();
        return Status::Ready;
    }
    void cancelDraftRead(const std::array<uint8_t, 16>& id) override
    {
        Guard guard;
        if (guard.locked)
        {
            if (session && session->draft_read && session->draft_read->id == id)
            {
                session->draft_read.reset();
                session->draft_io_reset = true;
            }
        }
        else cancel_draft_read.store(true);
        next_step.store(0);
    }
    bool saveDraft(::ui::geocaching::DraftInput& input) override
    {
        Guard guard;
        if (!guard.locked || !session || !session->store || session->needsRecovery() || session->store->commitPending() ||
            downloadActive() || publicationActive() || draftSaveActive() || session->phase == Phase::ResumeDownloads ||
            (session->client && session->client->persistencePending()) || input.generation == UINT64_MAX ||
            input.name.size() > 96 || input.description.size() > 2048 || input.hint.size() > 512) return false;
        gc::storage::DraftView draft;
        if (!input.generation && input.id == std::array<uint8_t, 16>{}) esp_fill_random(input.id.data(), input.id.size());
        draft.generation = input.generation + 1;
        draft.name = input.name;
        draft.description = input.description;
        draft.hint = input.hint;
        draft.latitude_e7 = input.latitude_e7;
        draft.longitude_e7 = input.longitude_e7;
        draft.has_coordinates = input.has_coordinates;
        draft.state = input.state;
        draft.difficulty_x2 = input.difficulty_x2;
        draft.terrain_x2 = input.terrain_x2;
        draft.container_size = input.container_size;
        auto job = std::unique_ptr<Session::DraftSave>(new (std::nothrow) Session::DraftSave);
        if (!job) return false;
        const auto capacity = input.name.size() + input.description.size() + input.hint.size() + 160;
        job->capacity = capacity;
        job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.draft", capacity, false));
        if (!job->bytes || !gc::storage::encodeDraft({input.id.data(), input.id.size()}, draft, job->bytes, capacity, job->size)) return false;
        job->id = input.id;
        job->expected = input.generation;
        session->draft_save = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    ::ui::geocaching::DraftSaveStatus draftSaveStatus(const std::array<uint8_t, 16>& id, uint64_t generation) override
    {
        using Status = ::ui::geocaching::DraftSaveStatus;
        Guard guard;
        if (!guard.locked) return Status::Pending;
        if (!session || !session->draft_save || session->draft_save->id != id || session->draft_save->expected != generation) return Status::Failed;
        const auto& job = *session->draft_save;
        return !job.done ? Status::Pending : job.saved ? Status::Saved
                                                       : Status::Failed;
    }
    bool download(const ::ui::geocaching::Item& item, uint64_t generation) override
    {
        Guard guard;
        if (!guard.locked || !session || session->phase != Phase::Ready || !session->source || !session->port || !session->download_store ||
            session->needsRecovery() || downloadActive() || publicationActive() || draftSaveActive() || session->store->commitPending()) return false;
        if (session->client->phase() != gc::QueryClientPhase::PageReady && session->client->phase() != gc::QueryClientPhase::Failed) return false;
        if (!session->saved || !session->saved->checked(item.id, item.revision_hash) || session->saved->contains(item.id, item.revision_hash)) return false;
        ::ui::geocaching::Snapshot snapshot;
        session->source->snapshot(::ui::geocaching::Section::Discover, snapshot);
        if ((generation ^ (epoch << 32)) != snapshot.generation) return false;
        gc::protocol::SummaryView summary;
        bool found = false;
        for (size_t i = 0; i < snapshot.count; ++i)
            if (session->port->summary(i, summary) && summary.id.bytes == item.id && summary.hash.bytes == item.revision_hash)
            {
                found = true;
                break;
            }
        gc::Destination remote;
        if (!found || !session->port->pageSource(remote)) return false;
        auto job = std::unique_ptr<Session::DownloadStart>(new (std::nothrow) Session::DownloadStart);
        if (!job || summary.name.size() > job->name.size()) return false;
        job->summary = summary;
        std::memcpy(job->name.data(), summary.name.data(), summary.name.size());
        job->summary.name = {job->name.data(), summary.name.size()};
        job->remote = remote;
        session->download_start = std::move(job);
        session->download_start_error = nullptr;
        ++epoch;
        next_step.store(0);
        return true;
    }
} facade;

// QueryClient does not restore an old page's request ID. Its durable read
// tasks must therefore stop before the dispatcher can send for a new session.
// Keep history and all publication/download tasks. Scan one pinned snapshot;
// each stop only appends to the currently scanned bucket, whose head is pinned.
bool retirePreviousQueries(Session& s, const gc::Destination& local)
{
    if (s.previous_queries_retired) return false;
    if (!s.previous_queries) s.previous_queries.reset(new (std::nothrow) Session::PreviousQueries(s.volume));
    if (!s.previous_queries || !s.workspace_owner.acquire(s.previous_queries.get()))
    {
        next_step.store(millis() + 1000);
        return true;
    }
    auto& job = *s.previous_queries;
    if (!job.started)
    {
        job.snapshot = s.roots[s.root_copy];
        gc::storage::IndexRootView pinned;
        if (!gc::storage::decodeIndexRoot({job.snapshot.data(), job.snapshot.size()}, s.volume, pinned) ||
            !job.scan.begin(pinned, 10, s.frame, kFrameCapacity))
        {
            fail("Cannot inspect previous queries");
            return true;
        }
        job.started = true;
        s.status = "Restoring previous queries...";
        ++epoch;
        return true;
    }
    if (job.stop)
    {
        const auto result = job.stop->step();
        if (result == IndexedCommitStep::Working) return true;
        if (result != IndexedCommitStep::Verified || !job.stop->committed(s.root))
        {
            s.checkpoint_recovery_required = true;
            fail("Previous query stop interrupted - reopen to recover");
            return true;
        }
        s.root_copy = 1 - s.root_copy;
        job.stop.reset();
        ++job.stopped;
        return true;
    }
    const auto result = job.scan.step();
    if (result == IndexScanStep::Working) return true;
    if (result == IndexScanStep::End)
    {
        if (job.stopped) Serial.printf("[Geocaching] retired_previous_queries=%u\n", static_cast<unsigned>(job.stopped));
        s.workspace_owner.release(&job);
        s.previous_queries.reset();
        s.previous_queries_retired = true;
        return false;
    }
    gc::storage::MutationView row;
    gc::storage::TaskView task;
    if (result != IndexScanStep::Item || !job.scan.item(row) || !gc::storage::decodeTask(row.key, row.value, task))
    {
        fail("Cannot read previous queries - reopen to recover");
        return true;
    }
    bool own = task.request_count != 0;
    for (size_t i = 0; i < task.request_count; ++i)
        own = own && !std::memcmp(task.requests[i].data, local.bytes.data(), local.bytes.size());
    if (own && task.kind == 3 && task.continue_intent && task.state != 3 && task.state != 5)
    {
        job.stop.reset(new (std::nothrow) SdIndexedStopTask(s.volume));
        if (!job.stop)
        {
            next_step.store(millis() + 1000);
            return true;
        }
        if (!job.stop->begin(s.root, s.root_copy, row.key, false, s.frame, kFrameCapacity, s.roots[1 - s.root_copy]))
        {
            s.checkpoint_recovery_required = true;
            fail("Cannot stop previous query - reopen to recover");
            return true;
        }
    }
    job.scan.advance();
    return true;
}

bool closeSession()
{
    if (!session) return true;
    // Closing still persists query cancellation and may finish an active
    // write. Preserve its inputs while USB owns the volume, without touching
    // storage or turning temporary ownership into a recovery fault.
    if (storage::sd_external_block_owner_active())
    {
        next_step.store(millis() + 1000);
        return false;
    }
    if (session->previous_queries)
    {
        session->checkpoint_recovery_required = true;
        session->workspace_owner.release(session->previous_queries.get());
        session->previous_queries.reset();
    }
    if (session->checkpoint)
    {
        // Closing may interrupt any maintenance phase. Both checkpoint and
        // journal recovery baselines remain intact; abandon borrowed cursors
        // and force recovery on the next activation instead of delaying exit.
        session->checkpoint_recovery_required = true;
        session->workspace_owner.release(session->checkpoint.get());
        session->checkpoint.reset();
    }
    // A restart may arrive during a write. Preserve its live inputs until the
    // current operation terminates, then let normal recovery reconcile it.
    if (!session->needsRecovery() && session->workspace_owner.holder())
    {
        WorkspaceSlice workspace_slice{*session};
        if (session->dispatch_store && session->workspace_owner.heldBy(session->dispatch_store.get()))
        {
            session->dispatcher->dispatchOne(now(nullptr));
            return false;
        }
        if (session->port && session->workspace_owner.heldBy(session->port.get()))
        {
            if (session->client->persistencePending()) session->client->tick(now(nullptr).monotonic_ms);
            else session->port->maintenanceStep();
            return false;
        }
        if (session->store && session->store->commitPending())
        {
            session->store->stepCommit();
            return false;
        }
        if (session->download_store && session->download_store->commitPending())
        {
            session->download_store->stepCommit();
            return false;
        }
    }
    if (session->saved) session->saved->releaseRead();
    // Read-only projections must not hold the shared lease while the query
    // client persists cancellation during shutdown.
    if (session->store) session->store->releaseDraftRead();
    if (session->download_store) session->download_store->releaseRead();
    if (session->draft_catalog) session->draft_catalog->reading = false;
    if (session->client && session->store && !session->needsRecovery())
    {
        WorkspaceSlice workspace_slice{*session};
        if (session->client->persistencePending())
        {
            session->client->tick(now(nullptr).monotonic_ms);
            return false;
        }
        if (session->dispatch_store->busy())
        {
            session->dispatcher->dispatchOne(now(nullptr));
            return false;
        }
        const auto phase = session->client->phase();
        if (phase == gc::QueryClientPhase::FindingDirectory || phase == gc::QueryClientPhase::CheckingCapabilities ||
            phase == gc::QueryClientPhase::Querying)
        {
            if (!session->client->cancel() || session->client->persistencePending()) return false;
        }
    }
    if (!router->bindGeocachingHandlers(nullptr, nullptr, nullptr)) return false;
    // Unbinding joins any router callback before clearing its pending payload.
    announcement_pending.store(false, std::memory_order_release);
    delete pending_reply.exchange(nullptr, std::memory_order_acq_rel);
    if (session->created_backend && router->backendForProtocol(chat::MeshProtocol::Reticulum) == session->created_backend &&
        router->backendProtocol() != chat::MeshProtocol::Reticulum && router->backendProtocol() != chat::MeshProtocol::RNode)
    {
        auto removed = router->takeServiceBackend(chat::MeshProtocol::Reticulum, session->created_backend);
        if (!removed) return false;
    }
    session.reset();
    active.store(false);
    ++epoch;
    return true;
}
} // namespace

void configure(chat::MeshAdapterRouter& value, LoraBoard& radio)
{
    router = &value;
    board = &radio;
    if (!mutex) mutex = xSemaphoreCreateMutex();
    esp_fill_random(boot.data(), boot.size());
    ::geocaching::ui::shell::bind(&facade);
}
bool queueDraftSave(const uint8_t id[16], const uint8_t* bytes, size_t size, uint64_t expected)
{
    Guard guard;
    gc::storage::DraftView draft;
    if (!guard.locked || !id || !session || !session->store || session->store->commitPending() || session->needsRecovery() ||
        downloadActive() || publicationActive() || draftSaveActive() || session->phase == Phase::ResumeDownloads ||
        (session->client && session->client->persistencePending()) ||
        !gc::storage::decodeDraft({id, 16}, {bytes, size}, draft) || expected == UINT64_MAX || draft.generation != expected + 1) return false;
    auto job = std::unique_ptr<Session::DraftSave>(new (std::nothrow) Session::DraftSave);
    if (!job) return false;
    job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.draft", size, false));
    if (!job->bytes) return false;
    std::memcpy(job->id.data(), id, 16);
    std::memcpy(job->bytes, bytes, size);
    job->size = size;
    job->capacity = size;
    job->editor_fields = false;
    job->expected = expected;
    session->draft_save = std::move(job);
    next_step.store(0);
    ++epoch;
    return true;
}
bool queuePublication(const uint8_t* bytes, size_t size)
{
    Guard guard;
    if (!guard.locked || !bytes || !size || size > 4166 || !session || session->phase != Phase::Ready ||
        !session->port || !session->store || session->store->commitPending() || session->needsRecovery() ||
        session->client->persistencePending() || downloadActive() || publicationActive() || draftSaveActive()) return false;
    gc::Destination remote;
    if (!session->port->pageSource(remote)) return false;
    auto job = std::unique_ptr<Session::Publication>(new (std::nothrow) Session::Publication);
    if (!job) return false;
    job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.publish", 2 * size + 26, false));
    if (!job->bytes) return false;
    std::memcpy(job->bytes, bytes, size);
    job->size = size;
    job->remote = remote;
    session->draft_save.reset();
    session->publication = std::move(job);
    next_step.store(0);
    ++epoch;
    return true;
}
bool workPending()
{
    if (!router || !board || !mutex) return false;
    if (!wanted.load() && !active.load()) return false;
    return static_cast<int32_t>(millis() - next_step.load()) >= 0;
}
void step()
{
    Guard guard;
    if (!guard.locked || !router || !board) return;
    // The shared owner already advances one bounded slice per scheduled tick
    // and yields its batch to chat/contacts. An extra per-slice timer makes
    // workPending() false inside that adapter, prematurely ending every batch.
    // Only genuinely idle/unavailable branches below need a retry deadline.
    next_step.store(millis());
    // Finish the small first-use volume header before closing; abandoning an
    // otherwise healthy initialization would leave an unrecoverable empty ledger.
    const bool finishing_initialization = session && session->phase >= Phase::Directories &&
                                          session->phase <= Phase::VerifyFormat && storage::sd_card_ready() && !storage::sd_external_block_owner_active();
    const bool recovering_download = session && session->phase == Phase::ResumeDownloads;
    if ((!wanted.load() && !finishing_initialization && !downloadActive() && !publicationActive() && !draftSaveActive() && !recovering_download) || restart.load())
    {
        if (closeSession()) restart.store(false);
        return;
    }
    if (!session)
    {
        session.reset(new (std::nothrow) Session);
        if (!session)
        {
            next_step.store(millis() + 2000);
            return;
        }
        active.store(true);
        ++epoch;
    }
    auto& s = *session;
    WorkspaceSlice workspace_slice{s};
    if (s.phase == Phase::Failed)
    {
        next_step.store(millis() + 2000);
        return;
    }
    if (s.needsRecovery())
    {
        fail("Storage interrupted - reopen to recover");
        return;
    }
    drainReply(s);
    if (cancel_draft_read.exchange(false))
    {
        s.draft_read.reset();
        s.draft_io_reset = true;
    }
    if (s.draft_io_reset)
    {
        if (s.store) s.store->releaseDraftRead();
        if (s.draft_catalog) s.draft_catalog->reading = false;
        s.publication_restore_pending = false;
        s.draft_io_reset = false;
    }
    if (s.draft_catalog && s.draft_catalog->reading &&
        (!s.draft_catalog_wanted || s.response || downloadActive() || publicationActive() || draftSaveActive()))
    {
        s.store->releaseDraftRead();
        s.draft_catalog->reading = false;
    }
    if (s.publication_restore_pending && s.publication_restore_background &&
        (s.response || downloadActive() || publicationActive() || draftSaveActive() ||
         (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending)))
    {
        s.store->releaseDraftRead();
        s.publication_restore_pending = false;
    }
    if (s.download_restore_pending &&
        (s.response || downloadActive() || publicationActive() || draftSaveActive() ||
         (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending)))
    {
        s.download_store->releaseRead();
        s.download_restore_pending = false;
    }
    if (s.saved && (s.response || downloadActive() || publicationActive() || draftSaveActive() || s.draft_catalog_wanted ||
                    (s.client && s.client->persistencePending()) ||
                    (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending) ||
                    !storage::sd_card_ready() || storage::sd_external_block_owner_active()))
    {
        const auto before = s.saved->generation();
        s.saved->releaseRead();
        if (before != s.saved->generation()) ++epoch;
    }
    if (!storage::sd_card_ready() || storage::sd_external_block_owner_active())
    {
        if (!s.notice)
        {
            s.notice = "SD card unavailable";
            if (s.saved) s.saved->reset();
            ++epoch;
        }
        next_step.store(millis() + 1000);
        if (s.draft_read)
        {
            s.draft_read->status = ::ui::geocaching::DraftReadStatus::Failed;
            s.draft_io_reset = true;
        }
        return;
    }
    if (s.notice)
    {
        s.notice = nullptr;
        ++epoch;
    }
    // The active holder must run before new foreground work waiting for it.
    // Query proof reads use the same lease as query persistence.
    if (advanceCheckpoint(s)) return;
    if (s.load_more_pending && !s.workspace_owner.holder())
    {
        s.load_more_pending = false;
        if (s.client && s.client->hasMore() && s.port && s.port->generation() == s.load_more_generation)
            s.source->loadMore();
        ++epoch;
        return;
    }
    if (s.dispatcher && s.dispatch_store->busy() && s.workspace_owner.heldBy(s.dispatch_store.get()))
    {
        const auto sent = s.dispatcher->dispatchOne(now(nullptr));
        if (sent.status == DispatchStatus::StorageBlocked || sent.status == DispatchStatus::Corrupt)
            fail("Dispatch storage is blocked");
        return;
    }
    if (s.port && s.workspace_owner.heldBy(s.port.get()))
    {
        if (s.client && s.client->persistencePending()) s.client->tick(now(nullptr).monotonic_ms);
        else s.port->maintenanceStep();
        return;
    }
    if (s.phase == Phase::Ready && !s.workspace_owner.holder() && processResponse(s)) return;
    // Continue the current read owner before background catalogs that would
    // otherwise wait on its lease. Foreground jobs cancelled it above.
    if (s.saved && s.saved->reading())
    {
        const auto before = s.saved->generation();
        const bool worked = s.saved->advance();
        if (before != s.saved->generation()) ++epoch;
        if (worked) return;
    }
    if (s.download_restore_pending && resumeWaitingDownload(s)) return;
    if (s.publication_restore_pending && s.publication_restore_background)
    {
        const auto restored = restorePublication(s);
        if (restored == PublicationRestore::Invalid)
        {
            fail("Saved publication could not be recovered");
            return;
        }
        if (restored == PublicationRestore::Pending || restored == PublicationRestore::Restored) return;
    }
    if (s.draft_catalog && s.draft_catalog->reading && advanceDraftCatalog(s)) return;
    if (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending)
    {
        auto& job = *s.draft_read;
        gc::ByteView bytes;
        gc::storage::DraftView draft;
        const auto result = s.store ? s.store->readDraft({job.id.data(), job.id.size()}, bytes) : DraftReadResult::Unavailable;
        if (result == DraftReadResult::Pending) return;
        if (result != DraftReadResult::Busy)
        {
            const bool valid = result == DraftReadResult::Ready && gc::storage::decodeDraft({job.id.data(), job.id.size()}, bytes, draft);
            if (valid)
            {
                job.bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.draft.read", bytes.size, false));
                if (job.bytes)
                {
                    std::memcpy(job.bytes, bytes.data, bytes.size);
                    job.size = bytes.size;
                }
            }
            if (s.store) s.store->releaseDraftRead();
            job.status = job.bytes ? ::ui::geocaching::DraftReadStatus::Ready : ::ui::geocaching::DraftReadStatus::Failed;
            return;
        }
        // Busy belongs to another job. Let its commit/dispatch advance below.
    }
    if (draftSaveActive())
    {
        auto& job = *s.draft_save;
        const auto result = job.started ? s.store->stepCommit() : job.editor_fields ? s.store->editDraft({job.id.data(), job.id.size()}, job.bytes, job.size, job.capacity, job.expected)
                                                                                    : s.store->saveDraft({job.id.data(), job.id.size()}, {job.bytes, job.size}, job.expected);
        if (result != JournalWriteResult::Busy)
        {
            job.started = true;
            if (result != JournalWriteResult::InProgress || s.store->inputConsumed())
            {
                heap_caps_free(job.bytes);
                job.bytes = nullptr;
            }
            if (result != JournalWriteResult::InProgress)
            {
                job.done = true;
                job.saved = result == JournalWriteResult::Verified;
                ++epoch;
            }
            return;
        }
        // Advance the operation that currently owns storage before retrying.
    }
    if (s.download_start && advanceDownloadStart(s)) return;
    if (s.saved && s.saved->pending() && !s.draft_catalog_wanted && s.phase != Phase::ResumeDownloads && !downloadActive() && !publicationActive() && !draftSaveActive() &&
        !s.store->commitPending() && !s.needsRecovery() && (!s.client || !s.client->persistencePending()))
    {
        const auto before = s.saved->generation();
        const bool worked = s.saved->advance();
        if (before != s.saved->generation()) ++epoch;
        if (worked) return;
    }
    if (!downloadActive() && !publicationActive() && !draftSaveActive() &&
        (!s.draft_read || s.draft_read->status != ::ui::geocaching::DraftReadStatus::Pending) &&
        s.store && !s.store->commitPending() && (!s.client || !s.client->persistencePending()) && advanceDraftCatalog(s)) return;
    if (s.phase == Phase::Failed)
    {
        next_step.store(millis() + 2000);
        return;
    }
    switch (s.phase)
    {
    case Phase::Inspect:
    {
        const auto result = inspectSdVolume(s.volume);
        if (result == SdVolumeResult::Ready) startRecovery();
        else if (result == SdVolumeResult::Missing) s.phase = Phase::CheckNew;
        else fail("Geocaching storage requires recovery");
        return;
    }
    case Phase::CheckNew:
        if (storage::sd_exists("/trailmate/geocaching/.state"))
        {
            fail("Interrupted storage initialization");
            return;
        }
        esp_fill_random(s.volume.data(), s.volume.size());
        s.phase = Phase::Directories;
        return;
    case Phase::Directories:
    {
        static constexpr const char* paths[] = {"/trailmate", "/trailmate/geocaching", "/trailmate/geocaching/caches",
                                                "/trailmate/geocaching/imports", "/trailmate/geocaching/exports", "/trailmate/geocaching/.state",
                                                "/trailmate/geocaching/.state/history", "/trailmate/geocaching/.state/journal",
                                                "/trailmate/geocaching/.state/checkpoint", "/trailmate/geocaching/.state/staging"};
        if (s.directory == sizeof(paths) / sizeof(paths[0]))
        {
            s.phase = Phase::OpenFormat;
            return;
        }
        if (s.mkdir_pending)
        {
            if (!storage::sd_mkdir(paths[s.directory]))
            {
                fail("Cannot create geocaching storage");
                return;
            }
            s.mkdir_pending = false;
            ++s.directory;
        }
        else if (storage::sd_is_directory(paths[s.directory])) ++s.directory;
        else s.mkdir_pending = true;
        return;
    }
    case Phase::OpenFormat:
        if (!s.format.open("/trailmate/geocaching/.state/format.bin", "w"))
        {
            fail("Cannot initialize storage");
            return;
        }
        s.phase = Phase::WriteFormat;
        return;
    case Phase::WriteFormat:
    {
        const auto header = gc::storage::encodeVolumeHeader(s.volume);
        if (s.format.write(header.data(), header.size()) != header.size())
        {
            fail("Storage write failed");
            return;
        }
        s.phase = Phase::FlushFormat;
        return;
    }
    case Phase::FlushFormat:
        if (!s.format.flush())
        {
            fail("Storage flush failed");
            return;
        }
        s.phase = Phase::CloseFormat;
        return;
    case Phase::CloseFormat:
        s.format.close();
        s.phase = Phase::VerifyFormat;
        return;
    case Phase::VerifyFormat:
    {
        gc::storage::VolumeInstance found;
        if (inspectSdVolume(found) != SdVolumeResult::Ready || found != s.volume)
        {
            fail("Storage verification failed");
            return;
        }
        if (!wanted.load())
        {
            s.phase = Phase::Inspect;
            return;
        }
        startRecovery();
        return;
    }
    case Phase::Recover:
    {
        const auto result = s.recovery->step();
        if (result == IndexedRecoveryStep::Working) return;
        if (result != IndexedRecoveryStep::Restored || !s.recovery->selected(s.root, s.root_copy))
        {
            fail(result == IndexedRecoveryStep::VolumeChanged ? "Storage volume changed during recovery"
                 : result == IndexedRecoveryStep::OutOfMemory ? "Insufficient memory for storage recovery"
                 : result == IndexedRecoveryStep::IoError     ? "Cannot read cached storage"
                                                              : "Cached index needs recovery or more workspace");
            return;
        }
        s.recovery.reset();
        if (!s.ensureBuffers(true))
        {
            fail("Insufficient storage workspace");
            return;
        }
        s.store.reset(new (std::nothrow) IndexedPublicationStore(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                 s.workspace_owner, s.workspace, s.frame, kFrameCapacity,
                                                                 s.payload, kPayloadCapacity, s.verification, kVerificationCapacity, s.crypto));
        s.download_store.reset(new (std::nothrow) IndexedDownloadStore(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                       s.workspace_owner, s.workspace, s.frame, kFrameCapacity,
                                                                       s.payload, kPayloadCapacity, s.verification, kVerificationCapacity, s.crypto));
        s.dispatch_store.reset(new (std::nothrow) IndexedDispatchStore(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                       s.workspace_owner, s.workspace, s.frame, kFrameCapacity));
        if (!s.store || !s.download_store || !s.dispatch_store || !s.workspace_owner.setPrepare(Session::prepareWorkspace, &s))
        {
            fail("Insufficient indexed storage memory");
            return;
        }
        s.saved.reset(new (std::nothrow) SavedCacheCatalog<Digest>(*s.download_store, s.crypto));
        if (!s.saved)
        {
            fail("Insufficient catalogue memory");
            return;
        }
        s.phase = Phase::ResumeDownloads;
        s.status = "Recovering downloaded GPX files...";
        ++epoch;
        return;
    }
    case Phase::ResumeDownloads:
    {
        if (s.download_port)
        {
            const auto result = s.download_port->poll();
            if (result == gc::DownloadOperationResult::Pending) return;
            if (result != gc::DownloadOperationResult::Complete && (!s.recovering_installed_download || s.download_store->needsRecovery()))
            {
                fail("Downloaded GPX recovery needs attention");
                return;
            }
            // A user-edited installed GPX or missing history remains untouched.
            // Its catalogue entry is checked separately; keep browsing other
            // caches instead of turning one offline file into a service outage.
            s.recovery_attention |= result != gc::DownloadOperationResult::Complete || s.download_port->historyPending();
            s.download_port.reset();
            s.saved->reset();
            ++epoch;
            return;
        }
        gc::storage::DownloadRecoveryRequest recovered;
        const auto selected = s.download_store->readRecovery(
            s.have_recovered_download ? gc::ByteView{s.recovered_download.data(), s.recovered_download.size()} : gc::ByteView{}, recovered);
        if (selected == DownloadRecoveryRead::Pending || selected == DownloadRecoveryRead::Busy) return;
        if (selected == DownloadRecoveryRead::Unavailable && !s.download_store->needsRecovery())
        {
            next_step.store(millis() + 1000);
            return;
        }
        if (selected != DownloadRecoveryRead::Ready && selected != DownloadRecoveryRead::End)
        {
            fail(selected == DownloadRecoveryRead::WorkspaceTooSmall ? "Insufficient download recovery workspace"
                 : selected == DownloadRecoveryRead::IoError         ? "Cannot read download recovery metadata"
                 : selected == DownloadRecoveryRead::VolumeChanged   ? "Download storage volume changed"
                                                                     : "Downloaded GPX metadata needs recovery");
            return;
        }
        if (selected == DownloadRecoveryRead::Ready)
        {
            gc::Destination local, remote;
            gc::RequestId request;
            std::memcpy(local.bytes.data(), recovered.key.data(), 16);
            std::memcpy(remote.bytes.data(), recovered.key.data() + 16, 16);
            std::memcpy(request.bytes.data(), recovered.key.data() + 32, 16);
            s.recovered_download = recovered.key;
            s.have_recovered_download = true;
            s.recovering_installed_download = recovered.installed;
            s.download_port.reset(new (std::nothrow) SdDownloadPort<Digest>(*s.download_store, s.crypto,
                                                                            local, recovered.identity, recovered.task, recovered.created));
            if (!s.download_port || s.download_port->resume(remote, request) != gc::DownloadOperationResult::Pending)
                fail("Cannot resume downloaded GPX");
            return;
        }
        s.phase = Phase::Connect;
        s.connect_since = now(nullptr).monotonic_ms;
        s.status = "Connecting to Reticulum...";
        ++epoch;
        return;
    }
    case Phase::Connect:
    {
        // A previous chat selection can leave an inactive Reticulum instance.
        // Release it outside the router lock before allocating the IP service.
        if (auto cached = router->takeInactiveReticulumCache()) return;
        if (!router->bindGeocachingHandlers(announcementReceived, responseReceived, nullptr)) return;
        if (!router->backendForProtocol(chat::MeshProtocol::Reticulum))
        {
            // The existing LXMF allocator uses PSRAM, not the internal heap.
            // Check its actual object size and contiguous capacity before creation.
            if (!mem::admit("geocaching.transport", sizeof(chat::reticulum::ReticulumAdapter) + 4096, 0,
                            sizeof(chat::lxmf::LxmfAdapter), 40 * 1024, 0, 4096))
            {
                fail("Insufficient transport memory");
                return;
            }
            auto backend = std::unique_ptr<chat::reticulum::ReticulumAdapter>(new (std::nothrow)
                                                                                  chat::reticulum::ReticulumAdapter(*board, nullptr, chat::reticulum::ReticulumUsage::BackgroundIpService));
            if (!backend)
            {
                fail("Cannot start Reticulum");
                return;
            }
            backend->applyConfig(app::AppContext::getInstance().readConfig().reticulumConfig());
            auto* created = backend.get();
            if (!router->installServiceBackend(chat::MeshProtocol::Reticulum, std::move(backend))) return;
            s.created_backend = created;
        }
        gc::Destination local;
        if (!router->getGeocachingDispatchDestination(local.bytes.data()))
        {
            if (now(nullptr).monotonic_ms - s.connect_since >= gc::QueryClient::kReplyTimeoutMs)
            {
                fail("Reticulum IP unavailable - check connection settings");
                return;
            }
            if (std::strcmp(s.status, "Waiting for Reticulum IP connection"))
            {
                s.status = "Waiting for Reticulum IP connection";
                ++epoch;
            }
            next_step.store(millis() + 500);
            return;
        }
        if (s.client)
        {
            if (local.bytes != s.local.bytes)
            {
                fail("Reticulum identity changed - reopen Geocaching");
                return;
            }
            s.phase = Phase::Ready;
            ++epoch;
            return;
        }
        if (retirePreviousQueries(s, local)) return;
        if (!s.ensureBuffers(true))
        {
            next_step.store(millis() + 1000);
            return;
        }
        if (!s.query_page) s.query_page = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.query.page", kPageCapacity, false));
        if (!s.query_page)
        {
            fail("Insufficient query page memory");
            return;
        }
        s.port.reset(new (std::nothrow) IndexedQueryStorePort(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1], local,
                                                              s.workspace_owner, s.workspace, s.frame, kFrameCapacity,
                                                              s.query_page, kPageCapacity, s.crypto, randomId, now, nullptr));
        s.local = local;
        if (!s.port)
        {
            fail("Insufficient memory");
            return;
        }
        s.client.reset(new (std::nothrow) gc::QueryClient(*s.port));
        s.dispatcher.reset(new (std::nothrow) RequestDispatcher(*router, *s.dispatch_store, 5000, 120000));
        if (!s.client || !s.dispatcher)
        {
            fail("Insufficient memory");
            return;
        }
        s.source.reset(new (std::nothrow) QueryBrowseSource(*s.client, *s.port, kWorld));
        if (!s.source)
        {
            fail("Insufficient memory");
            return;
        }
        s.client->query(kWorld);
        s.phase = Phase::Ready;
        ++epoch;
        return;
    }
    case Phase::Ready:
        break;
    case Phase::Failed:
        return;
    }
    if (s.needsRecovery())
    {
        fail("Storage interrupted - reopen to recover");
        return;
    }
    if (publicationActive())
    {
        auto& job = *s.publication;
        if (job.draft_stage != Session::Publication::DraftStage::None)
        {
            advanceDraftPublication(s);
            return;
        }
        if (job.bytes)
        {
            auto* scratch = job.bytes + job.size;
            gc::protocol::VerifiedRecordView record;
            std::array<uint8_t, 64> author;
            if (gc::protocol::verifyGeocache({job.bytes, job.size}, s.crypto, scratch, job.size + 26, record) == gc::protocol::VerificationResult::Valid &&
                router->getGeocachingAuthorKey(author.data()) && !std::memcmp(author.data(), record.record.author_public_key.data, 64))
            {
                const auto restored = restorePublication(s, &record.id, &record.hash, &job.remote);
                if (restored == PublicationRestore::Restored || restored == PublicationRestore::Pending) return;
                if (restored == PublicationRestore::Invalid)
                {
                    job.error = "Saved publication could not be recovered";
                    heap_caps_free(job.bytes);
                    job.bytes = nullptr;
                    ++epoch;
                    return;
                }
                std::array<uint8_t, 16> task;
                gc::RequestId request;
                esp_fill_random(task.data(), task.size());
                esp_fill_random(request.bytes.data(), request.bytes.size());
                job.port.reset(new (std::nothrow) SdPublishPort(*s.store, s.crypto, s.local, record.id, record.hash, task, now(nullptr)));
                if (job.port) job.attempt.reset(new (std::nothrow) gc::PublishAttempt(*job.port, s.crypto));
                if (job.attempt && !job.attempt->begin(job.remote, request, {job.bytes, job.size}, scratch, job.size + 26)) job.attempt.reset();
            }
            heap_caps_free(job.bytes);
            job.bytes = nullptr;
            job.started = now(nullptr).monotonic_ms;
            ++epoch;
            return;
        }
        const auto phase = job.attempt->phase();
        if (phase != gc::PublishAttemptPhase::Waiting)
        {
            job.attempt->advance();
            ++epoch;
            return;
        }
        if (now(nullptr).monotonic_ms - job.started >= job.wait_ms && !s.store->commitPending())
        {
            job.attempt->cancel();
            ++epoch;
            return;
        }
    }
    if (!s.publication && !downloadActive() && !s.store->commitPending() && !s.client->persistencePending())
    {
        const auto restored = restorePublication(s);
        if (restored == PublicationRestore::Invalid)
        {
            fail("Saved publication could not be recovered");
            return;
        }
        if (restored == PublicationRestore::Restored || restored == PublicationRestore::Pending) return;
    }
    if (!s.download && !publicationActive() && !s.store->commitPending() && !s.client->persistencePending() && resumeWaitingDownload(s)) return;
    if (s.download && (s.download->phase() == gc::DownloadPhase::Submitting || s.download->phase() == gc::DownloadPhase::Installing ||
                       s.download->phase() == gc::DownloadPhase::Cancelling))
    {
        const auto before = s.download->phase();
        s.download->advance();
        if (before != s.download->phase())
        {
            if (s.saved) s.saved->reset();
            ++epoch;
        }
        return;
    }
    if (auto cached = router->takeInactiveReticulumCache())
    {
        s.created_backend = nullptr;
        s.phase = Phase::Connect;
        s.connect_since = now(nullptr).monotonic_ms;
        s.status = "Reconnecting Reticulum service...";
        ++epoch;
        return;
    }
    const auto phase = s.client->phase();
    if (s.client->persistencePending())
    {
        s.client->tick(now(nullptr).monotonic_ms);
        return;
    }
    if (s.dispatch_store->busy())
    {
        s.dispatcher->dispatchOne(now(nullptr));
        return;
    }
    if (processResponse(s)) return;
    if (s.download && s.download->phase() == gc::DownloadPhase::Waiting &&
        now(nullptr).monotonic_ms - s.download_started >= s.download_wait_ms)
    {
        s.download->cancel();
        ++epoch;
        return;
    }
    if (announcement_pending.load(std::memory_order_acquire))
    {
        const auto& incoming = pending_announcement;
        const bool accepted = s.client->observe(incoming.discovery, incoming.delivery, {incoming.key.data(), incoming.key.size()},
                                                {incoming.data.data(), incoming.size}, now(nullptr).monotonic_ms);
        Serial.printf("[Geocaching][Discovery] directory_metadata accepted=%u\n", accepted ? 1U : 0U);
        announcement_pending.store(false, std::memory_order_release);
    }
    if (s.port->maintenancePending())
    {
        s.port->maintenanceStep();
        return;
    }
    if (publicationActive() || (s.download && s.download->phase() == gc::DownloadPhase::Waiting))
    {
        const auto sent = s.dispatcher->dispatchOne(now(nullptr));
        if (sent.status == DispatchStatus::StorageBlocked || sent.status == DispatchStatus::Corrupt)
            fail("Download storage is blocked");
        return;
    }
    if (phase == gc::QueryClientPhase::FindingDirectory)
    {
        if (!s.client->tick(now(nullptr).monotonic_ms)) next_step.store(millis() + 500);
        return;
    }
    if (s.client->tick(now(nullptr).monotonic_ms)) return;
    if (s.client->persistencePending()) return;
    if (s.client->phase() == gc::QueryClientPhase::Failed)
    {
        next_step.store(millis() + 2000);
        return;
    }
    if (startCheckpoint(s)) return;
    // Completed browsing has nothing to send. Active publication/download
    // dispatch is handled above; do not keep scanning attempt history here.
    if (s.client->phase() == gc::QueryClientPhase::PageReady)
    {
        next_step.store(millis() + 1000);
        return;
    }
    gc::Destination destination;
    gc::RequestId request;
    std::array<uint8_t, 48> preferred{};
    const bool foreground = s.client->pendingRequest(destination, request);
    if (foreground)
    {
        std::memcpy(preferred.data(), s.local.bytes.data(), 16);
        std::memcpy(preferred.data() + 16, destination.bytes.data(), 16);
        std::memcpy(preferred.data() + 32, request.bytes.data(), 16);
    }
    const auto sent = s.dispatcher->dispatchOne(now(nullptr), foreground ? gc::ByteView{preferred.data(), preferred.size()} : gc::ByteView{});
    if (sent.status == DispatchStatus::StorageBlocked || sent.status == DispatchStatus::Corrupt)
        fail("Query storage is blocked");
    else if (!s.dispatch_store->busy()) next_step.store(millis() + 250);
}
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
