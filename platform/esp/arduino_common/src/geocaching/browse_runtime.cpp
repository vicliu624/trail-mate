#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "app/app_context.h"
#include "geocaching/protocol/record_encoder.h"
#include "geocaching/storage/download_recovery.h"
#include "geocaching/storage/draft_publication.h"
#include "geocaching/storage/record_shape.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
#include "platform/esp/arduino_common/geocaching/author_issue_port.h"
#include "platform/esp/arduino_common/geocaching/cache_detail.h"
#include "platform/esp/arduino_common/geocaching/indexed_dispatch_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_download_store.h"
#include "platform/esp/arduino_common/geocaching/indexed_publication_store.h"
#include "platform/esp/arduino_common/geocaching/live_query_port.h"
#include "platform/esp/arduino_common/geocaching/query_browse_source.h"
#include "platform/esp/arduino_common/geocaching/request_dispatcher.h"
#include "platform/esp/arduino_common/geocaching/saved_cache_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_checkpoint_rotation.h"
#include "platform/esp/arduino_common/geocaching/sd_download_port.h"
#include "platform/esp/arduino_common/geocaching/sd_index_repair.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_remove_saved.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_stop_task.h"
#include "platform/esp/arduino_common/geocaching/sd_publish_port.h"
#include "platform/esp/arduino_common/geocaching/stored_reply_receipt.h"
#include "platform/esp/common/geocaching_crypto.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/esp/common/meshcore_runtime_compat.h"
#include "platform/memory/psram_ptr.h"
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
constexpr size_t kVerificationCapacity = gc::kMaxRecordBytes + 64;
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
// One producer (serialized router callbacks), one storage-owner consumer.
// The mailbox payload exists only while pending and is allocated in PSRAM.
std::atomic<Announcement*> pending_announcement{nullptr};
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
    uint64_t local_map_revision = 0;
    uint64_t saved_catalog_epoch = 0, draft_catalog_epoch = 0;
    struct Publication
    {
        enum class DraftStage : uint8_t
        {
            None,
            Bind,
            Binding,
            Encode,
            ArchiveLookup,
            Archive,
            ArchiveSaving,
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
        ::platform::memory::PsramPtr<DeviceAuthorIssuePort> author_port;
        ::platform::memory::PsramPtr<gc::AuthorIssue> issue;
        ::platform::memory::PsramPtr<SdIndexGet> archive_read;
        const char* error = nullptr;
        uint8_t* bytes = nullptr;
        size_t size = 0;
        gc::Destination remote;
        ::platform::memory::PsramPtr<SdPublishPort> port;
        ::platform::memory::PsramPtr<gc::PublishAttempt> attempt;
        uint64_t started = 0;
        uint32_t wait_ms = gc::QueryClient::kReplyTimeoutMs;
        bool submitted = false;
        ~Publication()
        {
            heap_caps_free(bytes);
            heap_caps_free(unsigned_bytes);
            heap_caps_free(draft_bytes);
        }
    };
    ::platform::memory::PsramPtr<Publication> publication;
    chat::MeshOperationFailure last_dispatch_failure = chat::MeshOperationFailure::None;
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
        bool erase = false;
        bool erase_download = false;
        std::array<uint8_t, 32> cache{}, hash{};
        ::platform::memory::PsramPtr<SdIndexedRemoveSaved> removal;
        ~DraftSave() { heap_caps_free(bytes); }
    };
    ::platform::memory::PsramPtr<DraftSave> draft_save;
    struct DraftRead
    {
        std::array<uint8_t, 16> id{};
        uint8_t* bytes = nullptr;
        size_t size = 0;
        ::ui::geocaching::DraftReadStatus status = ::ui::geocaching::DraftReadStatus::Pending;
        ~DraftRead() { heap_caps_free(bytes); }
    };
    ::platform::memory::PsramPtr<DraftRead> draft_read;
    bool draft_io_reset = false;
    struct DraftCatalog
    {
        gc::storage::DraftCatalogPage page;
        size_t requested_offset = 0, total = 0;
        uint64_t sequence = UINT64_MAX, generation = 1;
        bool reading = false, ready = false, failed = false;
    };
    ::platform::memory::PsramPtr<DraftCatalog> draft_catalog;
    bool draft_catalog_wanted = false;
    bool map_metadata_only = false;
    Phase phase = Phase::Inspect;
    const char* status = "Opening geocaching storage...";
    const char* notice = nullptr;
    const char* browse_status = "Connecting to Reticulum...";
    bool storage_requested = false;
    bool local_read_only = false, local_snapshot_attempted = false;
    uint8_t startup_read_retries = 0;
    size_t saved_offset = 0, saved_count = 0;
    gc::storage::VolumeInstance volume{};
    storage::SdRuntimeFile format;
    size_t directory = 0;
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
    uint8_t* response = nullptr;
    bool workspace_unavailable = false;
    size_t response_size = 0;
    ::platform::memory::PsramPtr<gc::protocol::VerifiedRecordView> response_verified;
    gc::Destination response_source;
    gc::Destination local;
    gc::RequestId response_id;
    uint8_t response_operation = 0;
    std::array<gc::storage::MutationView, 3> mutations{};
    ::platform::memory::PsramPtr<SdIndexRepair<Digest>> recovery;
    ::platform::memory::PsramPtr<SdCheckpointRotation<Digest>> checkpoint;
    uint64_t checkpoint_attempt_sequence = 0;
    bool current_index_upgrade_pending = false;
    uint32_t current_index_upgrade_retry_ms = 0;
    uint64_t current_index_upgrade_deferred_revision = 0;
    bool checkpoint_recovery_required = false;
    ::platform::memory::PsramPtr<IndexedPublicationStore> store;
    ::platform::memory::PsramPtr<IndexedDispatchStore> dispatch_store;
    ::platform::memory::PsramPtr<LiveQueryPort> port;
    ::platform::memory::PsramPtr<CacheDetail> detail, pending_detail;
    ::platform::memory::PsramPtr<StoredReplyReceipt> receipts;
    ::platform::memory::PsramPtr<gc::QueryClient> client;
    ::platform::memory::PsramPtr<RequestDispatcher> dispatcher;
    ::platform::memory::PsramPtr<QueryBrowseSource> source;
    ::platform::memory::PsramPtr<SavedCacheCatalog<Digest>> saved;
    ::platform::esp::common::EspGeocachingCrypto crypto;
    ::platform::memory::PsramPtr<IndexedDownloadStore> download_store;
    struct DownloadStart
    {
        gc::protocol::SummaryView summary;
        std::array<char, gc::kMaxNameBytes> name{};
        gc::Destination remote;
        gc::RequestId request;
        uint8_t* response = nullptr;
        size_t response_size = 0;
        ::platform::memory::PsramPtr<gc::protocol::VerifiedRecordView> verified;
        ~DownloadStart() { heap_caps_free(response); }
    };
    ::platform::memory::PsramPtr<DownloadStart> download_start;
    const char* download_start_error = nullptr;
    bool download_recovery_complete = false;
    bool download_restore_pending = false;
    ::platform::memory::PsramPtr<SdDownloadPort<Digest>> download_port;
    ::platform::memory::PsramPtr<gc::DownloadClient> download;
    std::array<uint8_t, 48> recovered_download{};
    bool have_recovered_download = false;
    bool recovering_installed_download = false, recovery_attention = false;
    uint64_t download_started = 0;
    uint32_t download_wait_ms = gc::QueryClient::kReplyTimeoutMs;
    size_t download_scratch = 0;
    chat::IMeshAdapter* created_backend = nullptr;
    bool ensureBuffers(bool operations, bool metadata = false)
    {
        const bool needs_verification = operations || metadata;
        const size_t missing = (!frame ? kFrameCapacity : 0) + (!encoded ? kEncodingCapacity : 0) +
                               (operations && !payload ? kPayloadCapacity : 0) + (needs_verification && !verification ? kVerificationCapacity : 0);
        // These buffers are PSRAM-only (no internal fallback below). Charging
        // an unrelated internal reserve permanently blocks local reads on L2.
        if (missing && !mem::admit("geocaching.index.io", 0, 0, missing, 0, 0))
        {
            workspace_unavailable = true;
            return false;
        }
        if (!frame) frame = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.read", kFrameCapacity, false));
        if (!encoded) encoded = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.encode", kEncodingCapacity, false));
        if (operations && !payload) payload = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.payload", kPayloadCapacity, false));
        if (needs_verification && !verification) verification = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.index.verify", kVerificationCapacity, false));
        workspace.outgoing = encoded;
        workspace_unavailable = !frame || !encoded || (operations && !payload) || (needs_verification && !verification);
        return !workspace_unavailable;
    }
    static bool prepareWorkspace(void* context, const void* owner)
    {
        auto& s = *static_cast<Session*>(context);
        const bool metadata = owner == s.download_store.get() && s.download_store->metadataRead();
        const bool needs_record = owner == s.store.get() || (owner == s.download_store.get() && !metadata);
        if (!s.ensureBuffers(needs_record, metadata)) return false;
        if (s.store) s.store->bindWorkspace(s.frame, s.payload, s.verification);
        if (s.download_store) s.download_store->bindWorkspace(s.frame, s.payload, s.verification);
        if (s.dispatch_store) s.dispatch_store->bindWorkspace(s.frame);
        if (s.receipts) s.receipts->bindWorkspace(s.frame);
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
               (dispatch_store && dispatch_store->needsRecovery());
    }
    ~Session()
    {
        workspace_owner.release(detail.get());
        detail.reset();
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
        receipts.reset();
        trimBuffers();
    }
};
chat::MeshAdapterRouter* router = nullptr;
LoraBoard* board = nullptr;
SemaphoreHandle_t mutex = nullptr;
::platform::memory::PsramPtr<Session> session;
std::array<uint8_t, 16> boot{};
std::atomic<bool> wanted{false}, active{false}, restart{false};
std::atomic<bool> network_requested{false};
std::atomic<bool> local_storage_requested{false};
std::atomic<bool> cancel_draft_read{false};
std::atomic<bool> cancel_detail{false};
std::atomic<uint32_t> next_step{0};
std::atomic<uint32_t> replies_seen{0}, replies_busy{0}, replies_queued{0};
uint32_t replies_processed = 0, replies_accepted = 0;
uint64_t epoch = 0;
void reportPublication(const char* event, const gc::PublishAttempt& attempt)
{
    const auto& id = attempt.cacheId().bytes;
    Serial.printf("[Geocaching][Publication] %s cache_prefix=%02x%02x%02x%02x revision=%lu state=%u\n",
                  event, id[0], id[1], id[2], id[3], static_cast<unsigned long>(attempt.revision()), static_cast<unsigned>(attempt.state()));
}
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
                            : owner.heldBy(session.receipts.get())       ? "query"
                            : owner.heldBy(session.dispatch_store.get()) ? "dispatch"
                            : owner.heldBy(session.download_store.get()) ? "download"
                            : owner.heldBy(session.store.get())          ? "publication"
                                                                         : "other";
        Serial.printf("[Geocaching] state=%u query=%u sequence=%llu lease=%s dispatch=%u response=%u proof=%u restore_pub=%u restore_download=%u rx=%lu busy=%lu queued=%lu processed=%lu accepted=%lu\n",
                      static_cast<unsigned>(session.phase), query, static_cast<unsigned long long>(session.root.sequence), lease,
                      session.dispatch_store && session.dispatch_store->busy(), session.response != nullptr,
                      session.receipts && session.receipts->pending(), session.publication_restore_pending, session.download_restore_pending,
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
           s.draft_catalog->sequence == s.store->catalogGeneration() && s.draft_catalog->page.offset == s.draft_catalog->requested_offset;
}
bool draftMetadataReady(const Session& s)
{
    return s.draft_catalog && s.draft_catalog->page.metadata_ready && s.store && !s.needsRecovery() &&
           s.draft_catalog->sequence == s.store->catalogGeneration() &&
           s.draft_catalog->page.offset == s.draft_catalog->requested_offset;
}
bool advanceDraftCatalog(Session& s)
{
    if (!s.draft_catalog_wanted || !s.store || s.needsRecovery() || (s.phase != Phase::Ready && s.phase != Phase::ResumeDownloads)) return false;
    if (!s.draft_catalog)
    {
        s.draft_catalog.reset(::platform::memory::createPsram<Session::DraftCatalog>());
        if (s.draft_catalog) s.draft_catalog_epoch = ++epoch;
    }
    if (!s.draft_catalog) return false;
    auto& catalog = *s.draft_catalog;
    if (s.map_metadata_only && draftMetadataReady(s))
    {
        if (catalog.reading)
        {
            s.store->releaseDraftRead();
            catalog.reading = false;
        }
        return false;
    }
    if (draftCatalogReady(s) || (catalog.failed && catalog.sequence == s.store->catalogGeneration())) return false;
    if (!catalog.reading)
    {
        catalog.sequence = s.store->catalogGeneration();
        catalog.ready = false;
        catalog.page.metadata_ready = false;
        catalog.failed = false;
    }
    const bool had_metadata = catalog.page.metadata_ready;
    const auto result = s.store->readDraftCatalog(catalog.requested_offset, s.crypto, catalog.page);
    if (!had_metadata && catalog.page.metadata_ready)
    {
        catalog.total = catalog.page.total;
        ++catalog.generation;
    }
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
    Serial.printf("[Geocaching][Storage] failed phase=%u sequence=%llu reason=%s\n",
                  static_cast<unsigned>(session->phase), static_cast<unsigned long long>(session->root.sequence), reason);
    session->local_read_only = false;
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
    session->draft_save.reset();
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
    const bool foreground = s.response || (s.detail && s.detail->state == CacheDetail::State::Saved) || downloadActive() || publicationActive() || draftSaveActive() ||
                            (s.saved && s.saved->pending() && !s.draft_catalog_wanted) ||
                            (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending) ||
                            (s.draft_catalog_wanted && !draftCatalogReady(s));
    if (foreground) s.checkpoint->yieldToForeground();
    const auto result = s.checkpoint->step();
    if (result == CheckpointRotationStep::Working) return true;
    if (result == CheckpointRotationStep::Busy || result == CheckpointRotationStep::Unavailable)
    {
        next_step.store(millis() + 1000);
        return true;
    }
    const auto previous_epoch = s.root.epoch;
    const bool complete = (result == CheckpointRotationStep::Complete || result == CheckpointRotationStep::Yielded) &&
                          s.checkpoint->selected(s.root, s.root_copy);
    if (complete && s.root.epoch != previous_epoch) s.current_index_upgrade_pending = false;
    if (result == CheckpointRotationStep::Deferred) s.current_index_upgrade_deferred_revision = s.root.revision;
    if (s.current_index_upgrade_pending) s.current_index_upgrade_retry_ms = millis() + 5000;
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
    const bool upgrade = s.current_index_upgrade_pending;
    if (upgrade && (s.current_index_upgrade_deferred_revision == s.root.revision ||
                    static_cast<int32_t>(millis() - s.current_index_upgrade_retry_ms) < 0)) return false;
    if (!upgrade && (s.root.sequence < s.checkpoint_attempt_sequence || s.root.sequence - s.checkpoint_attempt_sequence < interval)) return false;
    if (s.local_read_only ||
        s.workspace_owner.holder() || s.response || pending_announcement.load(std::memory_order_acquire) || downloadActive() || publicationActive() || draftSaveActive()) return false;
    if (upgrade && ((s.saved && s.saved->pending() && !s.draft_catalog_wanted) || (s.draft_catalog_wanted && !draftCatalogReady(s)) ||
                    (s.detail && s.detail->state == CacheDetail::State::Saved) ||
                    (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending))) return false;
    s.checkpoint.reset(::platform::memory::createPsram<SdCheckpointRotation<Digest>>(s.volume));
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
    if (!s.checkpoint->begin(s.roots[0], s.roots[1], s.root_copy, s.frame, kFrameCapacity, s.encoded, kEncodingCapacity, upgrade ? 0 : interval))
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
    s.download_port.reset(::platform::memory::createPsram<SdDownloadPort<Digest>>(*s.download_store, s.crypto, s.local,
                                                                                  recovered.identity, recovered.task, recovered.created));
    if (s.download_port) s.download.reset(::platform::memory::createPsram<gc::DownloadClient>(*s.download_port, s.crypto));
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
    if (job.response) request = job.request;
    else randomId(nullptr, request.bytes.data());
    s.download_port.reset(::platform::memory::createPsram<SdDownloadPort<Digest>>(*s.download_store, s.crypto, s.local,
                                                                                  gc::InstallIdentity{job.summary.id, job.summary.hash, generation}, task, now(nullptr)));
    if (s.download_port) s.download.reset(::platform::memory::createPsram<gc::DownloadClient>(*s.download_port, s.crypto));
    if (!s.download)
    {
        s.download_port.reset();
        s.download_store->releaseRead();
        next_step.store(millis() + 1000);
        return true;
    }
    const auto scratch = job.summary.signed_bytes + 64;
    const bool begun = s.download->begin(job.remote, request, job.summary, generation);
    if (begun && job.response)
    {
        s.response = job.response;
        job.response = nullptr;
        s.response_size = job.response_size;
        s.response_source = job.remote;
        s.response_operation = 3;
        s.response_verified = std::move(job.verified);
    }
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
    ++epoch;
    return true;
}

void announcementReceived(const chat::lxmf::GeocachingAnnouncementView& message, void*)
{
    if (!wanted.load() || pending_announcement.load(std::memory_order_acquire) || message.discovery_destination.size != 16 ||
        message.delivery_destination.size != 16 || message.public_key.size != 64 || message.app_data.size > 128 ||
        !message.discovery_destination.data || !message.delivery_destination.data || !message.public_key.data || !message.app_data.data) return;
    auto pending = ::platform::memory::PsramPtr<Announcement>(::platform::memory::createPsram<Announcement>());
    if (!pending) return;
    auto& out = *pending;
    std::memcpy(out.discovery.bytes.data(), message.discovery_destination.data, 16);
    std::memcpy(out.delivery.bytes.data(), message.delivery_destination.data, 16);
    std::memcpy(out.key.data(), message.public_key.data, 64);
    std::memcpy(out.data.data(), message.app_data.data, message.app_data.size);
    out.size = message.app_data.size;
    pending_announcement.store(pending.release(), std::memory_order_release);
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
    if (value == 3 && session->detail && session->detail->matches(source, request))
    {
        const bool accepted = session->detail->receive(source, request, {message.data.data, message.data.size}, session->crypto, owned);
        next_step.store(0);
        return accepted;
    }
    if (value == 0 || value == 2)
    {
        const gc::ByteView bytes{message.data.data, message.data.size};
        if (session->port->accepted(source, request, bytes)) return true;
        if (!session->client || !session->client->expectsResponse(source, request)) return false;
        const bool accepted = session->client->accept(source, bytes);
        if (accepted)
        {
            ++replies_accepted;
            next_step.store(0);
        }
        return accepted;
    }
    // A first response for current work has no durable receipt yet. Process
    // it directly instead of queuing a fruitless historical receipt lookup.
    const bool current_publication = value == 1 && session->publication && session->publication->attempt &&
                                     session->publication->attempt->expectsResponse(source, request);
    const bool current_download = value == 3 && session->download && session->download->expectsResponse(source, request);
    if (!current_publication && !current_download && session->receipts &&
        session->receipts->accepted(source, request, {message.data.data, message.data.size})) return true;
    if (value == 1 && (!session->publication || !session->publication->attempt ||
                       !current_publication || session->publication->attempt->phase() != gc::PublishAttemptPhase::Waiting || message.data.size > 512)) return false;
    if (value == 3 && (!session->download || session->download->phase() != gc::DownloadPhase::Waiting ||
                       !current_download || message.data.size > session->download_scratch)) return false;
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
    auto reply = ::platform::memory::PsramPtr<PendingReply>(::platform::memory::createPsram<PendingReply>());
    if (!reply) return false;
    reply->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.rx", message.data.size, false));
    if (!reply->bytes) return false;
    std::memcpy(reply->source.bytes.data(), message.source.data, 16);
    std::memcpy(reply->destination.bytes.data(), message.destination.data, 16);
    std::memcpy(reply->bytes, message.data.data, message.data.size);
    reply->size = message.data.size;
    pending_reply.store(reply.release(), std::memory_order_release);
    next_step.store(0);
    return false; // A queued reply is acknowledged after validation.
}

void drainReply(Session& s)
{
    if (s.response || !s.port) return;
    ::platform::memory::PsramPtr<PendingReply> reply(pending_reply.exchange(nullptr, std::memory_order_acq_rel));
    if (!reply) return;
    receiveResponse({{reply->source.bytes.data(), 16}, {reply->destination.bytes.data(), 16}, {}, {reply->bytes, reply->size}}, &reply->bytes);
}

bool processResponse(Session& s)
{
    if (!s.response || !s.dispatch_store || s.dispatch_store->busy()) return false;
    // The retained detail is already available while its durable download
    // request is still being committed. Do not consume it before Waiting.
    if (s.response_operation == 3 && s.download && s.download->phase() == gc::DownloadPhase::Submitting) return false;
    ++replies_processed;
    if (s.response_operation == 1 && s.publication && s.publication->attempt)
    {
        auto& attempt = *s.publication->attempt;
        const auto before = attempt.phase();
        if (attempt.accept(s.response_source, {s.response, s.response_size}) && before != gc::PublishAttemptPhase::Confirmed &&
            attempt.phase() == gc::PublishAttemptPhase::Confirmed)
        {
            ++s.local_map_revision;
            reportPublication("confirmed", attempt);
        }
        ++epoch;
    }
    else if (s.response_operation == 3 && s.download)
    {
        const auto before = s.download->phase();
        if (s.response_verified)
            s.download->acceptVerified(s.response_source, {s.response, s.response_size}, *s.response_verified);
        else
        {
            auto* scratch = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.verify", s.download_scratch, false));
            if (!scratch) return false;
            s.download->accept(s.response_source, {s.response, s.response_size}, scratch, s.download_scratch);
            heap_caps_free(scratch);
        }
        if (before != s.download->phase()) ++epoch;
    }
    heap_caps_free(s.response);
    s.response_verified.reset();
    s.response = nullptr;
    s.response_size = 0;
    return true;
}

// Browsing has no volume dependency. Run before every storage slice, including
// while saved data is recovering, unavailable, or held by USB.
void advanceBrowse(Session& s)
{
    if (!network_requested.load() && !s.client && !downloadActive() && !publicationActive()) return;
    if (auto cached = router->takeInactiveReticulumCache())
    {
        s.created_backend = nullptr;
        return;
    }
    if (!s.client || !router->backendForProtocol(chat::MeshProtocol::Reticulum))
    {
        if (!router->bindGeocachingHandlers(announcementReceived, responseReceived, nullptr)) return;
        if (!router->backendForProtocol(chat::MeshProtocol::Reticulum))
        {
            if (!mem::admit("geocaching.transport", sizeof(chat::reticulum::ReticulumAdapter) + 4096, 0,
                            sizeof(chat::lxmf::LxmfAdapter), 40 * 1024, 0, 4096))
            {
                s.browse_status = "Insufficient transport memory";
                return;
            }
            auto backend = std::unique_ptr<chat::reticulum::ReticulumAdapter>(new (std::nothrow)
                                                                                  chat::reticulum::ReticulumAdapter(*board, nullptr, chat::reticulum::ReticulumUsage::BackgroundIpService));
            if (!backend) return;
            backend->applyConfig(app::AppContext::getInstance().readConfig().reticulumConfig());
            auto* created = backend.get();
            if (!router->installServiceBackend(chat::MeshProtocol::Reticulum, std::move(backend))) return;
            s.created_backend = created;
        }
    }
    if (!s.client)
    {
        if (!router->getGeocachingDispatchDestination(s.local.bytes.data()))
        {
            s.browse_status = "Waiting for Reticulum IP connection";
            return;
        }
        if (!s.port) s.port.reset(::platform::memory::createPsram<LiveQueryPort>(s.crypto, randomId));
        if (!s.port) return;
        s.client.reset(::platform::memory::createPsram<gc::QueryClient>(*s.port));
        if (!s.client) return;
        s.source.reset(::platform::memory::createPsram<QueryBrowseSource>(*s.client, *s.port, kWorld));
        if (!s.source)
        {
            s.client.reset();
            return;
        }
        s.client->query(kWorld);
        ++epoch;
    }
    drainReply(s);
    if (auto* pending = pending_announcement.exchange(nullptr, std::memory_order_acq_rel))
    {
        ::platform::memory::PsramPtr<Announcement> owned(pending);
        const auto& incoming = *owned;
        s.client->observe(incoming.discovery, incoming.delivery, {incoming.key.data(), incoming.key.size()},
                          {incoming.data.data(), incoming.size}, now(nullptr).monotonic_ms);
    }
    s.client->tick(now(nullptr).monotonic_ms);
    const auto sent = s.port->dispatch(*router, s.local, now(nullptr).monotonic_ms);
    if (sent.failure != chat::MeshOperationFailure::None && sent.failure != s.last_dispatch_failure)
        Serial.printf("[Geocaching][Query] send_deferred failure=%u\n", static_cast<unsigned>(sent.failure));
    if (sent.ok || sent.failure != chat::MeshOperationFailure::None) s.last_dispatch_failure = sent.failure;
}

void startRecovery()
{
    auto& s = *session;
    if (!s.ensureBuffers(false))
    {
        fail("Insufficient storage workspace");
        return;
    }
    s.recovery.reset(::platform::memory::createPsram<SdIndexRepair<Digest>>(s.volume, s.roots[0], s.roots[1],
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

bool ensurePublicationStore(Session& s)
{
    if (s.store) return true;
    if (!s.ensureBuffers(true)) return false;
    s.store.reset(::platform::memory::createPsram<IndexedPublicationStore>(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                           s.workspace_owner, s.workspace, s.frame, kFrameCapacity,
                                                                           s.payload, kPayloadCapacity, s.verification, kVerificationCapacity, s.crypto));
    return s.store != nullptr;
}

void advanceStorageRecovery(Session& s)
{
    const auto result = s.recovery->step();
    // Map tiles and the local catalogue share SD access. A transient read
    // failure before any snapshot exists must not disable this session forever.
    // Bound retries; corruption and media changes still fail immediately.
    if (result == IndexedRecoveryStep::IoError && !s.local_snapshot_attempted && s.startup_read_retries < 2)
    {
        ++s.startup_read_retries;
        startRecovery();
        next_step.store(millis() + 250 * s.startup_read_retries);
        return;
    }
    if (result == IndexedRecoveryStep::Working)
    {
        if (s.local_snapshot_attempted || draftSaveActive() || downloadActive() || publicationActive() ||
            !s.recovery->readableSnapshot(s.root, s.root_copy)) return;
        s.local_snapshot_attempted = s.local_read_only = true;
        // The root bytes belong to Session. Do not keep the recovery engine and
        // its scratch leases resident for the lifetime of an offline page.
        s.recovery.reset();
    }
    else
    {
        const auto previous_sequence = s.root.sequence;
        if (result != IndexedRecoveryStep::Restored || !s.recovery->selected(s.root, s.root_copy))
        {
            fail(result == IndexedRecoveryStep::VolumeChanged ? "Storage volume changed during recovery"
                 : result == IndexedRecoveryStep::OutOfMemory ? "Insufficient memory for storage recovery"
                 : result == IndexedRecoveryStep::IoError     ? "Cannot read cached storage"
                                                              : "Cached index needs recovery or more workspace");
            return;
        }
        if (s.root.sequence != previous_sequence) s.draft_catalog.reset();
        s.current_index_upgrade_pending = s.recovery->needsCurrentIndexUpgrade();
        s.current_index_upgrade_retry_ms = millis();
        s.recovery.reset();
    }
    if (!s.ensureBuffers(false))
    {
        fail("Insufficient storage workspace");
        return;
    }
    s.download_store.reset(::platform::memory::createPsram<IndexedDownloadStore>(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                                 s.workspace_owner, s.workspace, s.frame, kFrameCapacity,
                                                                                 s.payload, kPayloadCapacity, s.verification, kVerificationCapacity, s.crypto));
    if ((!s.local_read_only || s.draft_catalog_wanted) && !ensurePublicationStore(s))
    {
        fail("Insufficient publication storage memory");
        return;
    }
    if (!s.download_store || !s.workspace_owner.setPrepare(Session::prepareWorkspace, &s))
    {
        fail("Insufficient indexed storage memory");
        return;
    }
    s.saved.reset(::platform::memory::createPsram<SavedCacheCatalog<Digest>>(*s.download_store, s.crypto));
    if (!s.saved)
    {
        fail("Insufficient catalogue memory");
        return;
    }
    s.saved_catalog_epoch = ++epoch;
    if (s.saved_count) s.saved->requestWindow(s.saved_offset, s.saved_count);
    s.phase = s.local_read_only ? Phase::Ready : Phase::ResumeDownloads;
    s.status = s.local_read_only ? "Saved storage ready" : "Recovering downloaded GPX files...";
    ++epoch;
}

// Use the same installed-record lookup as the saved list, but retain the full
// verified text for the open detail. No new files or persistent tasks.
bool advanceSavedDetail(Session& s)
{
    if (!s.detail || s.detail->state != CacheDetail::State::Saved) return false;
    auto& job = *s.detail;
    const auto finish = [&](const char* error)
    {
        job.saved_read.reset();
        s.workspace_owner.release(&job);
        if (error)
        {
            job.error = error;
            job.state = CacheDetail::State::Failed;
        }
        return true;
    };
    if (s.phase == Phase::Failed || s.needsRecovery()) return finish("Saved details need storage recovery.");
    if (!storage::sd_card_ready() || storage::sd_external_block_owner_active()) return finish("SD card unavailable. Go back and reopen to retry.");
    if (now(nullptr).monotonic_ms - job.started >= 120000) return finish("Saved details timed out. Go back and reopen to retry.");
    if (s.phase != Phase::Ready) return false;
    if (!job.saved_read)
    {
        if (downloadActive() || publicationActive() || draftSaveActive()) return false;
        if (!s.workspace_owner.acquire(&job)) return false;
        job.saved_read.reset(::platform::memory::createPsram<SdIndexedSavedCache>(s.volume));
        if (!job.saved_read || !job.saved_read->begin(s.root, {job.id.bytes.data(), job.id.bytes.size()}, true, s.frame, kFrameCapacity))
            return finish("Insufficient memory to read saved details.");
    }
    const auto result = job.saved_read->step();
    if (result == IndexScanStep::Working) return true;
    gc::storage::SavedCacheRecord saved;
    gc::ByteView signed_cache;
    if (result != IndexScanStep::Item || !job.saved_read->result(saved, signed_cache) ||
        saved.hash != job.hash.bytes || !job.verify(signed_cache, s.crypto))
        return finish("Saved details could not be verified.");
    return finish(nullptr);
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
    auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
    if (!job)
    {
        next_step.store(millis() + 1000);
        return PublicationRestore::Pending;
    }
    std::memcpy(job->remote.bytes.data(), selected.key.data() + 16, 16);
    job->cache = selected.cache;
    job->port.reset(::platform::memory::createPsram<SdPublishPort>(*s.store, s.crypto, s.local, selected.cache, selected.hash, selected.task, selected.created));
    if (job->port) job->attempt.reset(::platform::memory::createPsram<gc::PublishAttempt>(*job->port, s.crypto));
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
        Serial.printf("[Geocaching][Archive] failed cache_prefix=%02x%02x%02x%02x reason=%s\n", job.cache.bytes[0], job.cache.bytes[1], job.cache.bytes[2], job.cache.bytes[3], reason);
        job.archive_read.reset();
        s.workspace_owner.release(&job);
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
    if (job.draft_stage == Stage::ArchiveLookup)
    {
        if (!s.workspace_owner.acquire(&job)) return;
        std::array<uint8_t, 36> key{};
        std::memcpy(key.data(), job.cache.bytes.data(), 32);
        for (unsigned i = 0; i < 4; ++i) key[32 + i] = static_cast<uint8_t>(job.expected_revision >> ((3 - i) * 8));
        if (!job.archive_read)
        {
            job.archive_read.reset(::platform::memory::createPsram<SdIndexGet>(s.volume));
            if (!job.archive_read || !job.archive_read->begin(s.root, 3, {key.data(), key.size()}, s.frame, kFrameCapacity))
            {
                stop("Cannot read archive reservation");
                return;
            }
        }
        const auto status = job.archive_read->step();
        if (status == IndexGetStep::Working) return;
        if (status == IndexGetStep::Ready)
        {
            gc::storage::AuthorIssuedView prior;
            gc::RecordView record;
            gc::RevisionHash hash;
            gc::GeocacheId cache;
            size_t size = 0;
            if (!gc::storage::decodeAuthorIssued({key.data(), key.size()}, job.archive_read->value(), prior) || !prior.issued_at.has_utc ||
                !gc::protocol::decodeGeocacheRecord({job.unsigned_bytes, job.unsigned_size}, record))
            {
                stop("Invalid archive reservation");
                return;
            }
            record.updated_at = prior.issued_at.utc_seconds;
            if (!gc::protocol::encodeGeocacheRecord(record, job.bytes, job.size, size))
            {
                stop("Cannot restore archive version");
                return;
            }
            std::memcpy(job.unsigned_bytes, job.bytes, size);
            job.unsigned_size = size;
            if (gc::protocol::deriveGeocacheHashes({job.unsigned_bytes, size}, s.crypto, job.bytes, job.size, cache, hash) != gc::protocol::VerificationResult::Valid ||
                std::memcmp(hash.bytes.data(), prior.revision_hash.data, 32))
            {
                stop("Version already used; reopen latest cache");
                return;
            }
            job.issued = prior.issued_at;
        }
        else if (status != IndexGetStep::NotFound)
        {
            stop("Archive reservation read failed");
            return;
        }
        job.archive_read.reset();
        s.workspace_owner.release(&job);
        job.draft_stage = Stage::Archive;
        return;
    }
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
    if (job.draft_stage == Stage::ArchiveSaving)
    {
        const auto result = s.store->stepCommit();
        if (result == JournalWriteResult::Busy || result == JournalWriteResult::InProgress) return;
        if (result != JournalWriteResult::Verified)
        {
            stop("Local archive could not be saved");
            return;
        }
        heap_caps_free(job.draft_bytes);
        job.draft_bytes = nullptr;
        ++s.local_map_revision;
        job.draft_stage = Stage::Archive;
        job.draft_generation = UINT64_MAX;
        return;
    }
    if (job.draft_stage == Stage::Archive)
    {
        if (job.draft_generation != UINT64_MAX)
        {
            gc::RecordView record;
            gc::ByteView value;
            if (!gc::protocol::decodeGeocacheRecord({job.unsigned_bytes, job.unsigned_size}, record))
            {
                stop("Invalid archive record");
                return;
            }
            const auto read = s.store->readDraft(record.creation_nonce, value);
            if (read == DraftReadResult::Pending || read == DraftReadResult::Busy) return;
            if (read == DraftReadResult::Ready)
            {
                gc::storage::DraftView draft;
                if (!gc::storage::decodeDraft(record.creation_nonce, value, draft) || draft.generation == UINT64_MAX ||
                    draft.author.size != 64 || std::memcmp(draft.author.data, job.author.data(), 64))
                {
                    stop("Local author mismatch");
                    return;
                }
                const auto expected = draft.generation;
                draft.state = 2;
                ++draft.generation;
                job.draft_size = value.size + 32;
                job.draft_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.archive-draft", job.draft_size, false));
                if (!job.draft_bytes || !gc::storage::encodeDraft(record.creation_nonce, draft, job.draft_bytes, job.draft_size, job.draft_size))
                {
                    stop("Cannot retain local archive");
                    return;
                }
                s.store->releaseDraftRead();
                const auto saved = s.store->saveDraft(record.creation_nonce, {job.draft_bytes, job.draft_size}, expected);
                if (saved == JournalWriteResult::Busy)
                {
                    heap_caps_free(job.draft_bytes);
                    job.draft_bytes = nullptr;
                    return;
                }
                if (saved != JournalWriteResult::InProgress && saved != JournalWriteResult::Verified)
                {
                    stop("Local archive save failed");
                    return;
                }
                job.draft_stage = Stage::ArchiveSaving;
                if (saved == JournalWriteResult::Verified)
                {
                    heap_caps_free(job.draft_bytes);
                    job.draft_bytes = nullptr;
                    job.draft_generation = UINT64_MAX;
                    job.draft_stage = Stage::Archive;
                    ++s.local_map_revision;
                }
                return;
            }
            s.store->releaseDraftRead();
            if (read != DraftReadResult::NotFound)
            {
                stop("Cannot read local archive ownership");
                return;
            }
            job.draft_generation = UINT64_MAX;
        }
        job.author_port.reset(::platform::memory::createPsram<DeviceAuthorIssuePort>(*router, *s.store, s.crypto, job.issued));
        if (job.author_port) job.issue.reset(::platform::memory::createPsram<gc::AuthorIssue>(*job.author_port));
        if (!job.issue || !job.issue->begin({job.unsigned_bytes, job.unsigned_size}, job.bytes, job.size))
            stop("Cannot prepare archive signing");
        else job.draft_stage = Stage::Sign;
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
    job.author_port.reset(::platform::memory::createPsram<DeviceAuthorIssuePort>(*router, *s.store, s.crypto, job.issued, key, job.draft_generation));
    if (job.author_port) job.issue.reset(::platform::memory::createPsram<gc::AuthorIssue>(*job.author_port));
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
    network_requested.store(true);
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
                       !session->needsRecovery() && !session->store->commitPending() &&
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
    bool localMapRevision(uint64_t& out) override
    {
        Guard guard;
        if (!guard.locked || !session) return false;
        out = session->local_map_revision;
        return true;
    }
    void activate(bool open) override
    {
        if (open && !wanted.load())
        {
            network_requested.store(false);
            local_storage_requested.store(false);
        }
        wanted.store(open);
        if (!open) cancel_detail.store(true);
        next_step.store(0);
    }
    void snapshot(::ui::geocaching::Section section, ::ui::geocaching::Snapshot& out) override
    {
        out = {};
        if (section == ::ui::geocaching::Section::Discover) network_requested.store(true);
        else local_storage_requested.store(true);
        Guard guard;
        if (!guard.locked)
        {
            out.busy = true;
            std::snprintf(out.status.data(), out.status.size(), "Updating...");
            return;
        }
        if (session && section != ::ui::geocaching::Section::Discover)
        {
            session->storage_requested = true;
            if (section == ::ui::geocaching::Section::Published) session->draft_catalog_wanted = true;
        }
        if (section == ::ui::geocaching::Section::Published && session && (session->phase == Phase::Ready || session->phase == Phase::ResumeDownloads) && session->store && !session->needsRecovery())
        {
            session->draft_catalog_wanted = true;
            const auto* catalog = session->draft_catalog.get();
            out.count = catalog ? catalog->total : 0;
            out.generation = catalog ? catalog->generation : 0;
            out.ready = draftMetadataReady(*session);
            out.can_create = session->phase == Phase::Ready && !session->store->commitPending() && !downloadActive() && !publicationActive() && !draftSaveActive() &&
                             storage::sd_card_ready() && !storage::sd_external_block_owner_active();
            std::snprintf(out.status.data(), out.status.size(), "%s", catalog && catalog->failed ? "Draft list could not be read" : !draftMetadataReady(*session) ? "Loading local caches..."
                                                                                                                                : out.count                       ? "Drafts and saved publication status"
                                                                                                                                                                  : "No local drafts");
            if (!draftCatalogReady(*session) && (!catalog || !catalog->failed)) next_step.store(0);
        }
        else if (section == ::ui::geocaching::Section::Downloaded && session && (session->phase == Phase::Ready || session->phase == Phase::ResumeDownloads) && session->saved && !session->needsRecovery())
            session->saved->snapshot(out);
        else if (section == ::ui::geocaching::Section::Discover && session && session->source) session->source->snapshot(section, out);
        else
        {
            std::snprintf(out.status.data(), out.status.size(), "%s", session ? (section == ::ui::geocaching::Section::Discover ? session->browse_status : session->status) : "Starting Geocaching...");
            out.can_refresh = session && session->phase == Phase::Failed;
        }
        if (session && session->recovery_attention && section == ::ui::geocaching::Section::Downloaded)
            std::snprintf(out.status.data(), out.status.size(), "Some saved GPX files or history need attention");
        if (session && session->notice && section != ::ui::geocaching::Section::Discover) std::snprintf(out.status.data(), out.status.size(), "%s", session->notice);
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
            std::snprintf(out.status.data(), out.status.size(), "%s", job.erase   ? (!job.done ? "Deleting local cache..." : job.saved ? "Local cache deleted"
                                                                                                                                       : "Delete failed; cache retained")
                                                                      : !job.done ? "Saving local draft..."
                                                                      : job.saved ? "Draft saved locally"
                                                                                  : "Draft save failed; changes not saved");
            if (!job.done)
            {
                out.can_refresh = false;
                out.has_more = false;
            }
        }
        if (session && session->workspace_unavailable && section != ::ui::geocaching::Section::Discover)
            std::snprintf(out.status.data(), out.status.size(), "Waiting for storage workspace...");
        if (session && session->phase == Phase::Failed && section != ::ui::geocaching::Section::Discover)
        {
            std::snprintf(out.status.data(), out.status.size(), "%s", session->status);
            out.can_refresh = true;
            out.can_create = out.has_more = false;
        }
        // List ownership and its own content version determine row validity.
        // Transport progress and other sections must not invalidate local rows.
        const auto scope = section == ::ui::geocaching::Section::Published && session    ? session->draft_catalog_epoch
                           : section == ::ui::geocaching::Section::Downloaded && session ? session->saved_catalog_epoch
                                                                                         : epoch;
        out.generation ^= scope << 32;
    }
    void requestWindow(::ui::geocaching::Section section, size_t offset, size_t count) override
    {
        if (section == ::ui::geocaching::Section::Discover) network_requested.store(true);
        else local_storage_requested.store(true);
        Guard guard;
        if (!guard.locked || !session) return;
        session->map_metadata_only = false;
        if (section != ::ui::geocaching::Section::Discover) session->storage_requested = true;
        if (section == ::ui::geocaching::Section::Downloaded && count && count <= 4)
        {
            session->saved_offset = offset;
            session->saved_count = count;
        }
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
    void requestMapWindow(::ui::geocaching::Section section, size_t offset, size_t count) override
    {
        requestWindow(section, offset, count);
        Guard guard;
        if (guard.locked && session) session->map_metadata_only = true;
    }
    bool item(::ui::geocaching::Section section, size_t index, uint64_t generation, ::ui::geocaching::Item& out) override
    {
        Guard guard;
        if (guard.locked && section == ::ui::geocaching::Section::Published && session && session->store && !session->needsRecovery())
        {
            out = {};
            if (!draftMetadataReady(*session)) return false;
            const auto& catalog = *session->draft_catalog;
            if ((generation ^ (session->draft_catalog_epoch << 32)) != catalog.generation || index < catalog.page.offset || index - catalog.page.offset >= catalog.page.count) return false;
            const auto& draft = catalog.page.rows[index - catalog.page.offset];
            out.is_draft = true;
            out.state = draft.state;
            out.has_coordinates = draft.has_coordinates;
            out.edit_generation = draft.generation;
            std::memcpy(out.id.data(), draft.id.data(), 16);
            if (!draft.name[0]) std::snprintf(out.name.data(), out.name.size(), "Untitled draft");
            else out.name = draft.name;
            out.latitude_e7 = draft.latitude_e7;
            out.longitude_e7 = draft.longitude_e7;
            if (!draftCatalogReady(*session))
            {
                std::snprintf(out.detail.data(), out.detail.size(), "Saved locally\n%s\nChecking publication status...", draft.has_coordinates ? "Location set" : "Location not set");
                return true;
            }
            std::snprintf(out.detail.data(), out.detail.size(), "Local draft - not published\n%s\n%s", draft.has_coordinates ? "Location set" : "Location not set", draft.has_author ? "Author selected" : "Author not selected");
            const auto& publication = draft.publication;
            out.publication_revision = publication.latest_revision;
            out.publication_confirmed = publication.latest_revision && publication.confirmed_revision == publication.latest_revision && !publication.local_changes;
            if (publication.latest_revision)
            {
                if (draft.state == 2)
                {
                    std::snprintf(out.detail.data(), out.detail.size(), "%s\n%s",
                                  out.publication_confirmed && !publication.local_changes ? "Archived - confirmed by directory" : "Archive not confirmed",
                                  publication.local_changes ? "Local archive saved; publish to withdraw the public cache" : publication.pending ? "Awaiting directory confirmation"
                                                                                                                                                : "Retained locally; retry publication if needed");
                    return true;
                }
                std::snprintf(out.detail.data(), out.detail.size(), publication.confirmed_revision ? "v%lu %s%s\nLast directory-confirmed version: %lu" : "v%lu %s%s\nNo directory confirmation recorded",
                              static_cast<unsigned long>(publication.latest_revision), out.publication_confirmed ? "accepted by directory" : publication.pending ? "awaiting confirmation"
                                                                                                                                                                 : "stopped; result unconfirmed",
                              publication.local_changes ? "; local edits" : "",
                              static_cast<unsigned long>(publication.confirmed_revision));
            }
            return true;
        }
        if (guard.locked && section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->needsRecovery())
            return session->saved->item(index, generation ^ (session->saved_catalog_epoch << 32), out);
        if (!guard.locked || !session || !session->source || !session->source->item(section, index, generation ^ (epoch << 32), out)) return false;
        out.downloaded = session->saved && session->saved->contains(out.id, out.revision_hash);
        out.can_download = !out.downloaded && !downloadActive() && !publicationActive() && !draftSaveActive() && (!session->store || !session->store->commitPending()) &&
                           !session->needsRecovery() && session->phase != Phase::Failed && storage::sd_card_ready() && !storage::sd_external_block_owner_active() &&
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
        else if (section != ::ui::geocaching::Section::Discover && session && session->phase == Phase::Failed) restart.store(true);
        else if (session && session->source) session->source->refresh(section);
        next_step.store(0);
    }
    bool loadMore() override
    {
        Guard guard;
        if (!guard.locked || downloadActive() || publicationActive() || draftSaveActive() || !session || !session->source) return false;
        const bool begun = session->source->loadMore();
        if (begun) next_step.store(0);
        return begun;
    }
    void open(const ::ui::geocaching::Item& item, uint64_t) override
    {
        Guard guard;
        if (!guard.locked || !session || item.is_draft) return;
        auto job = ::platform::memory::PsramPtr<CacheDetail>(::platform::memory::createPsram<CacheDetail>());
        if (!job) return;
        job->id.bytes = item.id;
        job->hash.bytes = item.revision_hash;
        job->started = now(nullptr).monotonic_ms;
        if (item.downloaded)
        {
            job->state = CacheDetail::State::Saved;
            session->storage_requested = true;
        }
        else if (!session->port || !session->port->pageSource(job->remote) ||
                 !randomId(nullptr, job->request.bytes.data()) ||
                 !gc::protocol::encodeGetRequest(job->request, job->id, &job->hash, nullptr, 8192,
                                                 job->request_bytes.data(), job->request_bytes.size(), job->request_size))
            job->state = CacheDetail::State::Failed;
        session->pending_detail = std::move(job);
        cancel_detail.store(false);
        next_step.store(0);
    }
    bool readDetail(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash,
                    void (*sink)(const ::ui::geocaching::DetailView&, void*), void* context) override
    {
        Guard guard;
        if (!guard.locked || !session || !sink) return false;
        const auto* job = session->pending_detail ? session->pending_detail.get() : session->detail.get();
        ::ui::geocaching::DetailView view;
        if (session->publication && session->publication->cache.bytes == id && session->publication->error)
            view.archive_error = session->publication->error;
        if (!job || cancel_detail.load() || !job->matches(id, hash))
        {
            view.status = ::ui::geocaching::DetailStatus::Failed;
            view.error = "Details unavailable. Go back and reopen to retry.";
            sink(view, context);
            return true;
        }
        if (job->state == CacheDetail::State::Ready)
        {
            view.status = ::ui::geocaching::DetailStatus::Ready;
            view.description = job->description.data();
            view.hint = job->hint.data();
            std::array<uint8_t, 64> author{};
            view.can_archive = job->verified_record.record.state != gc::CacheState::Archived &&
                               router->getGeocachingAuthorKey(author.data()) &&
                               !std::memcmp(author.data(), job->verified_record.record.author_public_key.data, author.size());
        }
        else if (job->state == CacheDetail::State::Failed)
        {
            view.status = ::ui::geocaching::DetailStatus::Failed;
            view.error = job->error;
        }
        sink(view, context);
        return true;
    }
    void closeDetail() override
    {
        cancel_detail.store(true);
        next_step.store(0);
    }
    bool archiveCache(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash) override
    {
        Guard guard;
        if (!guard.locked || !session || !session->detail || session->detail->state != CacheDetail::State::Ready ||
            !session->detail->matches(id, hash) || !session->port || session->needsRecovery() ||
            publicationActive() || downloadActive() || draftSaveActive()) return false;
        const auto& verified = session->detail->verified_record;
        auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
        if (!job || verified.record.revision == UINT32_MAX || verified.record.state == gc::CacheState::Archived ||
            !router->getGeocachingAuthorKey(job->author.data()) ||
            std::memcmp(job->author.data(), verified.record.author_public_key.data, job->author.size()) ||
            !session->port->pageSource(job->remote)) return false;
        auto record = verified.record;
        ++record.revision;
        record.previous_hash = {hash.data(), hash.size()};
        record.state = gc::CacheState::Archived;
        record.updated_at = std::max(record.updated_at, static_cast<uint64_t>(std::time(nullptr)));
        job->issued = now(nullptr);
        job->issued.has_utc = true;
        job->issued.utc_seconds = record.updated_at;
        job->cache = verified.id;
        job->expected_revision = record.revision;
        const size_t capacity = record.name.size() + record.description.size() + record.hint.size() + 240;
        job->unsigned_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.archive", capacity, false));
        job->size = capacity + 70;
        job->bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.sign", 2 * job->size + 26, false));
        if (!job->unsigned_bytes || !job->bytes || !gc::protocol::encodeGeocacheRecord(record, job->unsigned_bytes, capacity, job->unsigned_size)) return false;
        job->draft_stage = Session::Publication::DraftStage::ArchiveLookup;
        session->storage_requested = true;
        session->publication = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    ::ui::geocaching::DraftSaveStatus archiveStatus(const std::array<uint8_t, 32>& id) override
    {
        using Status = ::ui::geocaching::DraftSaveStatus;
        Guard guard;
        if (!guard.locked) return Status::Pending;
        if (!session || !session->publication || session->publication->cache.bytes != id) return Status::Failed;
        const auto& job = *session->publication;
        if (job.error) return Status::Failed;
        if (job.draft_stage != Session::Publication::DraftStage::None || job.bytes) return Status::Pending;
        if (!job.attempt) return Status::Failed;
        if (job.attempt->phase() == gc::PublishAttemptPhase::Confirmed) return Status::Saved;
        return publicationActive() ? Status::Pending : Status::Failed;
    }
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
        auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
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
            session->draft_read.reset(::platform::memory::createPsram<Session::DraftRead>());
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
            input.generation == UINT64_MAX ||
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
        auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
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
        if (!session || !session->draft_save || session->draft_save->erase_download || session->draft_save->id != id || session->draft_save->expected != generation) return Status::Failed;
        const auto& job = *session->draft_save;
        return !job.done ? Status::Pending : job.saved ? Status::Saved
                                                       : Status::Failed;
    }
    bool deleteDraft(const std::array<uint8_t, 16>& id, uint64_t generation) override
    {
        Guard guard;
        if (!guard.locked || !generation || !session || session->phase != Phase::Ready || !session->store ||
            session->needsRecovery() || session->store->commitPending() || downloadActive() || publicationActive() || draftSaveActive()) return false;
        auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
        if (!job) return false;
        job->id = id;
        job->expected = generation;
        job->erase = true;
        session->draft_save = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    bool removeDownloaded(const ::ui::geocaching::Item& item) override
    {
        Guard guard;
        if (!guard.locked || item.is_draft || !item.downloaded || !session || session->phase != Phase::Ready ||
            session->needsRecovery() || downloadActive() || publicationActive() || draftSaveActive() ||
            (session->store && session->store->commitPending())) return false;
        auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
        if (!job) return false;
        job->erase_download = true;
        job->cache = item.id;
        job->hash = item.revision_hash;
        session->draft_save = std::move(job);
        next_step.store(0);
        ++epoch;
        return true;
    }
    ::ui::geocaching::DraftSaveStatus downloadedRemovalStatus(const std::array<uint8_t, 32>& id, const std::array<uint8_t, 32>& hash) override
    {
        using Status = ::ui::geocaching::DraftSaveStatus;
        Guard guard;
        if (!guard.locked) return Status::Pending;
        if (!session || !session->draft_save || !session->draft_save->erase_download ||
            session->draft_save->cache != id || session->draft_save->hash != hash) return Status::Failed;
        const auto& job = *session->draft_save;
        return !job.done ? Status::Pending : job.saved ? Status::Saved
                                                       : Status::Failed;
    }
    bool download(const ::ui::geocaching::Item& item, uint64_t generation) override
    {
        Guard guard;
        if (!guard.locked || !session || session->phase == Phase::Failed || !session->source || !session->port || !storage::sd_card_ready() || storage::sd_external_block_owner_active() ||
            session->needsRecovery() || downloadActive() || publicationActive() || draftSaveActive() || (session->store && session->store->commitPending())) return false;
        if (session->client->phase() != gc::QueryClientPhase::PageReady && session->client->phase() != gc::QueryClientPhase::Failed) return false;
        if (session->saved && session->saved->contains(item.id, item.revision_hash)) return false;
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
        auto job = ::platform::memory::PsramPtr<Session::DownloadStart>(::platform::memory::createPsram<Session::DownloadStart>());
        if (!job || summary.name.size() > job->name.size()) return false;
        job->summary = summary;
        std::memcpy(job->name.data(), summary.name.data(), summary.name.size());
        job->summary.name = {job->name.data(), summary.name.size()};
        job->remote = remote;
        auto* detail = session->detail.get();
        if (!session->response && detail && detail->state == CacheDetail::State::Ready && detail->response &&
            detail->matches(item.id, item.revision_hash) && detail->remote.bytes == remote.bytes)
        {
            job->verified.reset(::platform::memory::createPsram<gc::protocol::VerifiedRecordView>());
            if (!job->verified) return false;
            job->request = detail->request;
            job->response_size = detail->response_size;
            job->response = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.download.detail", job->response_size, false));
            if (!job->response) return false;
            std::memcpy(job->response, detail->response, job->response_size);
            // Both owners may outlive the other. Reparse the immutable copy so
            // every borrowed field belongs to the download's response, while
            // retaining the already verified identity/hash without more crypto.
            gc::protocol::GetResponseView parsed;
            if (!gc::protocol::decodeGetResponse({job->response, job->response_size}, job->request, 8192, parsed) || parsed.has_conflict) return false;
            gc::protocol::CmpReader reader(parsed.signed_cache);
            size_t count = 0;
            gc::ByteView encoded;
            if (!reader.array(count, 2) || count != 2 || !reader.binary(encoded, gc::kMaxRecordBytes) ||
                !reader.binary(job->verified->signature, 64) || job->verified->signature.size != 64 || !reader.finished() ||
                !gc::protocol::decodeGeocacheRecord(encoded, job->verified->record)) return false;
            job->verified->id = detail->verified_record.id;
            job->verified->hash = detail->verified_record.hash;
        }
        session->storage_requested = true;
        session->download_start = std::move(job);
        session->download_start_error = nullptr;
        ++epoch;
        next_step.store(0);
        return true;
    }
} facade;

bool advanceOfflineRemoval(Session& s)
{
    if (!s.draft_save || !s.draft_save->erase_download || s.draft_save->done) return false;
    auto& job = *s.draft_save;
    if (!s.workspace_owner.acquire(&job)) return false;
    auto finish = [&](bool saved)
    {
        job.done = true;
        job.saved = saved;
        job.removal.reset();
        s.workspace_owner.release(&job);
        if (saved)
        {
            if (s.saved) s.saved->reset();
            ++s.local_map_revision;
            Serial.printf("[Geocaching][LocalDelete] confirmed cache_prefix=%02x%02x%02x%02x\n", job.cache[0], job.cache[1], job.cache[2], job.cache[3]);
        }
        ++epoch;
        return true;
    };
    if (!job.removal)
    {
        job.removal.reset(::platform::memory::createPsram<SdIndexedRemoveSaved>(s.volume));
        if (!job.removal || !job.removal->begin(s.root, s.root_copy, job.cache, job.hash, s.frame, kFrameCapacity, s.roots[1 - s.root_copy]))
            return finish(false);
    }
    const auto result = job.removal->step();
    if (result == IndexedCommitStep::Working) return true;
    if (result == IndexedCommitStep::Verified)
    {
        gc::storage::IndexRootView next;
        if (!job.removal->committed(next)) return finish(false);
        s.root = next;
        s.root_copy = 1 - s.root_copy;
        return finish(true);
    }
    if (result == IndexedCommitStep::IoError || result == IndexedCommitStep::VolumeChanged || result == IndexedCommitStep::RecoveryRequired)
        s.checkpoint_recovery_required = true;
    return finish(false);
}

DispatchResult dispatchForeground(Session& s)
{
    std::array<uint8_t, 48> key{};
    const bool publication = s.publication && s.publication->port && s.publication->attempt &&
                             s.publication->attempt->phase() == gc::PublishAttemptPhase::Waiting && s.publication->port->dispatchKey(key);
    const bool download = !publication && s.download && s.download_port &&
                          s.download->phase() == gc::DownloadPhase::Waiting && s.download_port->dispatchKey(key);
    const auto result = s.dispatcher->dispatchOne(now(nullptr), publication || download ? gc::ByteView{key.data(), key.size()} : gc::ByteView{});
    if (publication && result.status == DispatchStatus::Submitted)
    {
        s.publication->submitted = true;
        s.publication->started = now(nullptr).monotonic_ms;
        reportPublication("submitted", *s.publication->attempt);
    }
    return result;
}

bool closeSession()
{
    if (!session) return true;
    // Closing still persists query cancellation and may finish an active
    // write. Preserve its inputs while USB owns the volume, without touching
    // storage or turning temporary ownership into a recovery fault.
    if (session->storage_requested && storage::sd_external_block_owner_active())
    {
        next_step.store(millis() + 1000);
        return false;
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
        if (session->draft_save && session->draft_save->erase_download && session->workspace_owner.heldBy(session->draft_save.get()))
        {
            advanceOfflineRemoval(*session);
            return false;
        }
        if (session->dispatch_store && session->workspace_owner.heldBy(session->dispatch_store.get()))
        {
            dispatchForeground(*session);
            return false;
        }
        if (session->receipts && session->workspace_owner.heldBy(session->receipts.get()))
        {
            session->receipts->step();
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
    // Release projection cursors before destroying their shared workspace.
    if (session->store) session->store->releaseDraftRead();
    if (session->download_store) session->download_store->releaseRead();
    if (session->draft_catalog) session->draft_catalog->reading = false;
    if (!router->bindGeocachingHandlers(nullptr, nullptr, nullptr)) return false;
    // Unbinding joins any router callback before clearing its pending payload.
    ::platform::memory::destroyPsram(pending_announcement.exchange(nullptr, std::memory_order_acq_rel));
    ::platform::memory::destroyPsram(pending_reply.exchange(nullptr, std::memory_order_acq_rel));
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
        !gc::storage::decodeDraft({id, 16}, {bytes, size}, draft) || expected == UINT64_MAX || draft.generation != expected + 1) return false;
    auto job = ::platform::memory::PsramPtr<Session::DraftSave>(::platform::memory::createPsram<Session::DraftSave>());
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
        downloadActive() || publicationActive() || draftSaveActive()) return false;
    gc::Destination remote;
    if (!session->port->pageSource(remote)) return false;
    auto job = ::platform::memory::PsramPtr<Session::Publication>(::platform::memory::createPsram<Session::Publication>());
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
        session.reset(::platform::memory::createPsram<Session>());
        if (!session)
        {
            next_step.store(millis() + 2000);
            return;
        }
        active.store(true);
        ++epoch;
    }
    auto& s = *session;
    const bool close_detail = cancel_detail.exchange(false);
    s.storage_requested |= local_storage_requested.load();
    if (close_detail || s.pending_detail)
    {
        s.workspace_owner.release(s.detail.get());
        s.detail.reset();
        if (close_detail) s.pending_detail.reset();
        else s.detail = std::move(s.pending_detail);
    }
    advanceBrowse(s);
    if (s.detail) s.detail->advanceNetwork(*router, s.local, s.crypto, now(nullptr).monotonic_ms);
    if (!s.storage_requested)
    {
        next_step.store(millis() + 100);
        return;
    }
    WorkspaceSlice workspace_slice{s};
    if (s.phase == Phase::Ready && s.draft_catalog_wanted && !ensurePublicationStore(s))
    {
        next_step.store(millis() + 1000);
        return;
    }
    if (s.local_snapshot_attempted && (s.local_read_only || s.recovery))
    {
        if (!storage::sd_card_ready() || storage::sd_external_block_owner_active())
        {
            next_step.store(millis() + 1000);
            return;
        }
        if (s.local_read_only && (draftSaveActive() || downloadActive() || publicationActive() ||
                                  s.needsRecovery() || (s.saved && s.saved->error()) || (s.draft_catalog && s.draft_catalog->failed)))
        {
            // The audit and local readers share a frame lease. Ready details
            // own their response independently; release only their reader.
            s.workspace_owner.release(s.detail.get());
            if (s.detail && s.detail->state == CacheDetail::State::Ready) s.detail->saved_read.reset();
            else s.detail.reset();
            s.pending_detail.reset();
            if (s.store) s.store->releaseDraftRead();
            s.saved.reset();
            if (s.draft_catalog)
            {
                auto& catalog = *s.draft_catalog;
                catalog.reading = false;
                if (catalog.failed)
                {
                    catalog.ready = catalog.failed = false;
                    catalog.page.metadata_ready = false;
                    ++catalog.generation;
                }
            }
            s.draft_read.reset();
            s.store.reset();
            s.download_store.reset();
            s.local_read_only = false;
            startRecovery();
        }
        if (s.phase == Phase::Recover)
        {
            advanceStorageRecovery(s);
            return;
        }
    }
    if (s.detail && s.detail->state == CacheDetail::State::Saved &&
        (s.phase == Phase::Failed || !storage::sd_card_ready() || storage::sd_external_block_owner_active()))
        advanceSavedDetail(s);
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
    // Saved reply receipts share the lease with persistent operations.
    if (advanceCheckpoint(s)) return;
    if (s.detail && s.workspace_owner.heldBy(s.detail.get()) && advanceSavedDetail(s)) return;
    if (s.dispatcher && s.dispatch_store->busy() && s.workspace_owner.heldBy(s.dispatch_store.get()))
    {
        const auto sent = dispatchForeground(s);
        if (sent.status == DispatchStatus::StorageBlocked || sent.status == DispatchStatus::Corrupt)
            fail("Dispatch storage is blocked");
        return;
    }
    if (s.receipts && s.workspace_owner.heldBy(s.receipts.get()))
    {
        s.receipts->step();
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
    if (s.phase == Phase::Ready && draftSaveActive())
    {
        auto& job = *s.draft_save;
        if (job.erase_download)
        {
            if (advanceOfflineRemoval(s)) return;
        }
        else
        {
            const auto result = job.started ? s.store->stepCommit() : job.erase       ? s.store->deleteDraft({job.id.data(), job.id.size()}, job.expected)
                                                                  : job.editor_fields ? s.store->editDraft({job.id.data(), job.id.size()}, job.bytes, job.size, job.capacity, job.expected)
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
                    if (job.saved) ++s.local_map_revision;
                    ++epoch;
                }
                return;
            }
        }
        // Advance the operation that currently owns storage before retrying.
    }
    if (s.phase == Phase::Ready && s.download_start && advanceDownloadStart(s)) return;
    if (advanceSavedDetail(s)) return;
    if (s.saved && s.saved->pending() && !s.draft_catalog_wanted && !(s.phase == Phase::ResumeDownloads && s.download_port) && !downloadActive() && !publicationActive() && !draftSaveActive() &&
        (!s.store || !s.store->commitPending()) && !s.needsRecovery())
    {
        const auto before = s.saved->generation();
        const bool worked = s.saved->advance();
        if (before != s.saved->generation()) ++epoch;
        if (worked) return;
    }
    if (!downloadActive() && !publicationActive() && !draftSaveActive() &&
        (!s.draft_read || s.draft_read->status != ::ui::geocaching::DraftReadStatus::Pending) &&
        s.store && !s.store->commitPending() && advanceDraftCatalog(s)) return;
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
        if (result == SdVolumeResult::Busy) return;
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
        const auto result = inspectSdVolume(found);
        if (result == SdVolumeResult::Busy) return;
        if (result != SdVolumeResult::Ready || found != s.volume)
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
        advanceStorageRecovery(s);
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
            // Keep its installed metadata available for browsing instead of
            // turning one offline file into a service outage.
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
            s.download_port.reset(::platform::memory::createPsram<SdDownloadPort<Digest>>(*s.download_store, s.crypto,
                                                                                          local, recovered.identity, recovered.task, recovered.created));
            if (!s.download_port || s.download_port->resume(remote, request) != gc::DownloadOperationResult::Pending)
                fail("Cannot resume downloaded GPX");
            return;
        }
        s.phase = Phase::Ready;
        s.status = "Saved storage ready";
        ++epoch;
        return;
    }
    case Phase::Ready:
        if (s.local_read_only)
        {
            next_step.store(millis() + 100);
            return;
        }
        break;
    case Phase::Failed:
        return;
    }
    if (s.needsRecovery())
    {
        fail("Storage interrupted - reopen to recover");
        return;
    }
    // Local lists, details, drafts and interrupted file installation above
    // do not need an online identity or request services. Only resume network
    // work once the real local destination is known.
    if (!s.client)
    {
        if (!startCheckpoint(s)) next_step.store(millis() + 100);
        return;
    }
    if (!s.dispatch_store)
        s.dispatch_store.reset(::platform::memory::createPsram<IndexedDispatchStore>(s.volume, s.root, s.root_copy, s.roots[0], s.roots[1],
                                                                                     s.workspace_owner, s.workspace, s.frame, kFrameCapacity));
    if (s.dispatch_store && !s.dispatcher)
        s.dispatcher.reset(::platform::memory::createPsram<RequestDispatcher>(*router, *s.dispatch_store, 5000, 120000));
    if (!s.receipts)
        s.receipts.reset(::platform::memory::createPsram<StoredReplyReceipt>(s.volume, s.root, s.local, s.workspace_owner, s.crypto));
    if (!s.dispatcher || !s.receipts)
    {
        s.browse_status = "Waiting for network service memory";
        next_step.store(millis() + 1000);
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
                job.port.reset(::platform::memory::createPsram<SdPublishPort>(*s.store, s.crypto, s.local, record.id, record.hash, task, now(nullptr)));
                if (job.port) job.attempt.reset(::platform::memory::createPsram<gc::PublishAttempt>(*job.port, s.crypto));
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
            if (job.attempt->phase() == gc::PublishAttemptPhase::Confirmed)
            {
                ++s.local_map_revision;
                reportPublication("confirmed", *job.attempt);
            }
            ++epoch;
            return;
        }
        // Durable preparation and transport admission have their own bounded
        // wait. They must not consume the directory response budget.
        const auto timeout = job.submitted ? job.wait_ms : 120000;
        if (now(nullptr).monotonic_ms - job.started >= timeout && !s.store->commitPending())
        {
            job.error = job.submitted ? "Directory did not confirm; retry" : "Request could not be sent; retry";
            reportPublication(job.submitted ? "confirmation_timeout" : "submission_timeout", *job.attempt);
            job.attempt->cancel();
            ++epoch;
            return;
        }
    }
    if (!s.publication && !downloadActive() && !s.store->commitPending())
    {
        const auto restored = restorePublication(s);
        if (restored == PublicationRestore::Invalid)
        {
            fail("Saved publication could not be recovered");
            return;
        }
        if (restored == PublicationRestore::Restored || restored == PublicationRestore::Pending) return;
    }
    if (!s.download && !publicationActive() && !s.store->commitPending() && resumeWaitingDownload(s)) return;
    if (s.download && (s.download->phase() == gc::DownloadPhase::Submitting || s.download->phase() == gc::DownloadPhase::Installing ||
                       s.download->phase() == gc::DownloadPhase::Cancelling))
    {
        const auto before = s.download->phase();
        s.download->advance();
        if (before != s.download->phase())
        {
            // Queuing, waiting and cancelling requests do not change saved
            // rows. Installation may have committed even if cleanup failed.
            if (before == gc::DownloadPhase::Installing)
            {
                ++s.local_map_revision;
                if (s.saved) s.saved->reset();
            }
            ++epoch;
        }
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
    if (s.receipts && s.receipts->pending())
    {
        s.receipts->step();
        return;
    }
    if (publicationActive() || (s.download && s.download->phase() == gc::DownloadPhase::Waiting))
    {
        const auto sent = dispatchForeground(s);
        if (sent.status == DispatchStatus::StorageBlocked || sent.status == DispatchStatus::Corrupt)
            fail("Download storage is blocked");
        return;
    }

    if (!startCheckpoint(s)) next_step.store(millis() + 100);
}
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
