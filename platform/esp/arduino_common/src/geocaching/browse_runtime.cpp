#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "app/app_context.h"
#include "geocaching/protocol/record_encoder.h"
#include "geocaching/storage/draft_publication.h"
#include "geocaching/storage/record_shape.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"
#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_adapter.h"
#include "platform/esp/arduino_common/geocaching/author_issue_port.h"
#include "platform/esp/arduino_common/geocaching/query_browse_source.h"
#include "platform/esp/arduino_common/geocaching/query_store_port.h"
#include "platform/esp/arduino_common/geocaching/request_dispatcher.h"
#include "platform/esp/arduino_common/geocaching/saved_cache_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_download_port.h"
#include "platform/esp/arduino_common/geocaching/sd_publish_port.h"
#include "platform/esp/arduino_common/geocaching/sd_state_recovery.h"
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
constexpr size_t kStateCapacity = 4096, kFrameCapacity = 4096;
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
        }
    };
    std::unique_ptr<Publication> publication;
    struct DraftSave
    {
        std::array<uint8_t, 16> id{};
        uint8_t* bytes = nullptr;
        size_t size = 0;
        uint64_t expected = 0;
        bool started = false, done = false, saved = false;
        ~DraftSave() { heap_caps_free(bytes); }
    };
    std::unique_ptr<DraftSave> draft_save;
    struct DraftRead
    {
        std::array<uint8_t, 16> id{};
        uint64_t generation = 0;
        ::ui::geocaching::DraftReadStatus status = ::ui::geocaching::DraftReadStatus::Pending;
    };
    std::unique_ptr<DraftRead> draft_read;
    Phase phase = Phase::Inspect;
    const char* status = "Opening geocaching storage...";
    const char* notice = nullptr;
    gc::storage::VolumeInstance volume{};
    storage::SdRuntimeFile format;
    size_t directory = 0;
    uint64_t connect_since = 0;
    bool mkdir_pending = false;
    uint8_t *first = nullptr, *second = nullptr, *frame = nullptr, *response = nullptr;
    size_t response_size = 0;
    gc::Destination response_source;
    gc::Destination local;
    gc::RequestId response_id;
    uint8_t response_operation = 0;
    std::unique_ptr<Announcement> announcement;
    std::array<gc::storage::MutationView, 3> mutations{};
    std::unique_ptr<gc::storage::LogicalState> state;
    std::unique_ptr<SdStateRecovery<Digest>> recovery;
    std::unique_ptr<SdRequestStore> store;
    std::unique_ptr<QueryBrowsePort> port;
    std::unique_ptr<gc::QueryClient> client;
    std::unique_ptr<RequestDispatcher> dispatcher;
    std::unique_ptr<QueryBrowseSource> source;
    std::unique_ptr<SavedCacheCatalog<Digest>> saved;
    ::platform::esp::common::EspGeocachingCrypto crypto;
    std::unique_ptr<SdDownloadPort<Digest>> download_port;
    std::unique_ptr<gc::DownloadClient> download;
    uint64_t download_started = 0;
    uint32_t download_wait_ms = gc::QueryClient::kReplyTimeoutMs;
    size_t download_scratch = 0;
    chat::IMeshAdapter* created_backend = nullptr;
    ~Session()
    {
        if (store && store->commitPending()) store->cancelCommit();
        source.reset();
        saved.reset();
        download.reset();
        download_port.reset();
        publication.reset();
        dispatcher.reset();
        client.reset();
        port.reset();
        store.reset();
        recovery.reset();
        state.reset();
        heap_caps_free(response);
        heap_caps_free(frame);
        heap_caps_free(first);
        heap_caps_free(second);
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
uint64_t epoch = 0;
bool downloadActive()
{
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
    session->publication.reset();
    session->download.reset();
    session->download_port.reset();
    session->phase = Phase::Failed;
    session->status = reason;
    ++epoch;
}

bool resumeWaitingDownload(Session& s)
{
    const auto view = s.state->view();
    size_t cursor = 0;
    gc::storage::MutationView row;
    while (view.next(cursor, row))
    {
        if (row.table != 5 || row.key.size != 48 || std::memcmp(row.key.data, s.local.bytes.data(), 16)) continue;
        gc::storage::OutgoingView outgoing;
        gc::storage::TaskView task;
        gc::ByteView value;
        if (!gc::storage::decodeOutgoing(row.key, row.value, outgoing) || outgoing.state >= 4 ||
            !outgoing.continue_intent || !view.find(10, outgoing.task_id, value) ||
            !gc::storage::decodeTask(outgoing.task_id, value, task) || task.kind != 2 ||
            !task.continue_intent || task.state == 3 || task.state == 5 ||
            !s.store->downloadIntentActive(row.key, outgoing.install_generation)) continue;
        gc::protocol::SummaryView summary;
        size_t scan = 0;
        gc::storage::MutationView query_row;
        bool found = false;
        while (!found && view.next(scan, query_row))
        {
            if (query_row.table != 5 || query_row.key.size != 48 || std::memcmp(query_row.key.data, row.key.data, 32)) continue;
            gc::storage::OutgoingView query;
            gc::protocol::QueryPageView page;
            gc::RequestId query_id;
            std::memcpy(query_id.bytes.data(), query_row.key.data + 32, 16);
            if (!gc::storage::decodeOutgoing(query_row.key, query_row.value, query) || query.state != 4 ||
                !gc::protocol::decodeQueryPage(query.terminal_data, query_id, 8192, 64, page)) continue;
            gc::protocol::CmpReader items(page.encoded_items);
            for (size_t i = 0; i < page.count; ++i)
            {
                if (!gc::protocol::decodeSummary(items, summary)) break;
                if (task.cache_id.size == 32 && task.revision_hash.size == 32 &&
                    !std::memcmp(summary.id.bytes.data(), task.cache_id.data, 32) &&
                    !std::memcmp(summary.hash.bytes.data(), task.revision_hash.data, 32))
                {
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            fail("Download preview missing - recovery needs attention");
            return true;
        }
        gc::Destination remote;
        gc::RequestId request;
        std::array<uint8_t, 16> task_id;
        std::memcpy(remote.bytes.data(), row.key.data + 16, 16);
        std::memcpy(request.bytes.data(), row.key.data + 32, 16);
        std::memcpy(task_id.data(), outgoing.task_id.data, 16);
        s.download_port.reset(new (std::nothrow) SdDownloadPort<Digest>(*s.store, *s.state, s.crypto, s.local,
                                                                        {summary.id, summary.hash, outgoing.install_generation}, task_id, outgoing.created));
        if (s.download_port) s.download.reset(new (std::nothrow) gc::DownloadClient(*s.download_port, s.crypto));
        if (!s.download || s.download_port->resumeWaiting(remote, request) != gc::DownloadOperationResult::Complete ||
            !s.download->resume(remote, request, summary, outgoing.install_generation))
        {
            fail("Cannot resume download request");
            return true;
        }
        s.download_started = now(nullptr).monotonic_ms;
        // Allow the dispatcher to expire an attempt from the previous boot,
        // retry it, and then give the response its normal timeout window.
        s.download_wait_ms = 2 * gc::QueryClient::kReplyTimeoutMs + 5000;
        s.download_scratch = summary.signed_bytes + 64;
        ++epoch;
        return true;
    }
    return false;
}

void announcementReceived(const chat::lxmf::GeocachingAnnouncementView& message, void*)
{
    Guard guard;
    if (!guard.locked || !session || !wanted.load() || message.discovery_destination.size != 16 ||
        message.delivery_destination.size != 16 || message.public_key.size != 64 || message.app_data.size > 128) return;
    if (!session->announcement) session->announcement.reset(new (std::nothrow) Announcement);
    if (!session->announcement) return;
    auto& out = *session->announcement;
    std::memcpy(out.discovery.bytes.data(), message.discovery_destination.data, 16);
    std::memcpy(out.delivery.bytes.data(), message.delivery_destination.data, 16);
    std::memcpy(out.key.data(), message.public_key.data, 64);
    std::memcpy(out.data.data(), message.app_data.data, message.app_data.size);
    out.size = message.app_data.size;
    next_step.store(0);
}
bool responseReceived(const chat::lxmf::CustomDeliveryView& message, void*)
{
    Guard guard;
    if (!guard.locked || !session || !session->port || (!wanted.load() && !downloadActive() && !publicationActive()) || message.source.size != 16 ||
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
    if (session->port->accepted(source, request, {message.data.data, message.data.size})) return true;
    if (value == 1 && (!session->publication || !session->publication->attempt ||
                       session->publication->attempt->phase() != gc::PublishAttemptPhase::Waiting || message.data.size > 512)) return false;
    if (value == 3 && (!session->download || session->download->phase() != gc::DownloadPhase::Waiting ||
                       message.data.size > session->download_scratch)) return false;
    if (value != 3 && message.data.size > 2048) return false;
    if (session->response) return false;
    auto* bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.rx", message.data.size, false));
    if (!bytes) return false;
    std::memcpy(bytes, message.data.data, message.data.size);
    session->response = bytes;
    session->response_size = message.data.size;
    session->response_source = source;
    session->response_id = request;
    session->response_operation = static_cast<uint8_t>(value);
    next_step.store(0);
    // The sender can retry; only the exact committed response is acknowledged.
    return false;
}

bool recoveredStateValid(const gc::storage::LogicalState::View& view)
{
    size_t cursor = 0;
    gc::storage::MutationView row;
    while (view.next(cursor, row))
        if (!gc::storage::validStoredRowShape(row)) return false;
    return session && session->state && gc::storage::validateAuthorHistory(session->state->view(), view) &&
           gc::storage::validateTaskReferences(view) && gc::storage::validateAttemptReferences(view);
}
void startRecovery()
{
    auto& s = *session;
    if (!mem::admit("geocaching", 4096, 0, kStateCapacity * 2 + kFrameCapacity, 40 * 1024, 0))
    {
        fail("Insufficient memory for browsing");
        return;
    }
    s.first = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.state", kStateCapacity, false));
    s.second = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.candidate", kStateCapacity, false));
    s.frame = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.recovery", kFrameCapacity, false));
    if (!s.first || !s.second || !s.frame)
    {
        fail("Insufficient storage workspace");
        return;
    }
    s.state.reset(new (std::nothrow) gc::storage::LogicalState(s.first, s.second, kStateCapacity));
    if (!s.state)
    {
        fail("Insufficient memory");
        return;
    }
    s.recovery.reset(new (std::nothrow) SdStateRecovery<Digest>(s.volume, *s.state));
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
    Restored,
    Invalid
};
PublicationRestore restorePublication(Session& s, const gc::GeocacheId* cache = nullptr,
                                      const gc::RevisionHash* hash = nullptr, const gc::Destination* remote = nullptr)
{
    const auto view = s.state->view();
    size_t cursor = 0;
    gc::storage::MutationView row, selected;
    bool found = false;
    while (view.next(cursor, row))
    {
        if (row.table != 5 || row.key.size != 48 || std::memcmp(row.key.data, s.local.bytes.data(), 16) ||
            (remote && std::memcmp(row.key.data + 16, remote->bytes.data(), 16))) continue;
        gc::storage::OutgoingView outgoing;
        gc::storage::TaskView task;
        gc::ByteView value;
        if (!gc::storage::decodeOutgoing(row.key, row.value, outgoing) || !view.find(10, outgoing.task_id, value) ||
            !gc::storage::decodeTask(outgoing.task_id, value, task) || task.kind != 1) continue;
        if (!gc::storage::requestBelongsToTask(outgoing.task_id, task, row.key, outgoing) || task.cache_id.size != 32 || task.revision_hash.size != 32)
            return PublicationRestore::Invalid;
        if ((cache && std::memcmp(task.cache_id.data, cache->bytes.data(), 32)) ||
            (hash && std::memcmp(task.revision_hash.data, hash->bytes.data(), 32))) continue;
        if (outgoing.state > 4 || (outgoing.state == 4 && !cache) ||
            (outgoing.state < 4 && (!outgoing.continue_intent || !task.continue_intent || task.state == 3 || task.state == 5))) continue;
        if (!found || outgoing.state == 4)
        {
            selected = row;
            found = true;
        }
        if (outgoing.state == 4) break;
    }
    if (!found) return PublicationRestore::None;
    gc::storage::OutgoingView outgoing;
    gc::storage::TaskView task;
    gc::ByteView value;
    if (!gc::storage::decodeOutgoing(selected.key, selected.value, outgoing) || !view.find(10, outgoing.task_id, value) ||
        !gc::storage::decodeTask(outgoing.task_id, value, task)) return PublicationRestore::Invalid;
    gc::GeocacheId id;
    gc::RevisionHash revision;
    gc::RequestId request;
    std::array<uint8_t, 16> task_id;
    std::memcpy(id.bytes.data(), task.cache_id.data, 32);
    std::memcpy(revision.bytes.data(), task.revision_hash.data, 32);
    std::memcpy(request.bytes.data(), selected.key.data + 32, 16);
    std::memcpy(task_id.data(), outgoing.task_id.data, 16);
    auto job = std::unique_ptr<Session::Publication>(new (std::nothrow) Session::Publication);
    if (!job) return PublicationRestore::Invalid;
    std::memcpy(job->remote.bytes.data(), selected.key.data + 16, 16);
    job->port.reset(new (std::nothrow) SdPublishPort(*s.store, s.crypto, s.local, id, revision, task_id, outgoing.created));
    if (job->port) job->attempt.reset(new (std::nothrow) gc::PublishAttempt(*job->port, s.crypto));
    if (!job->attempt || !job->port->attachRestoredRequest(job->remote, request)) return PublicationRestore::Invalid;
    bool restored = false;
    s.state->withScratch([&](uint8_t* scratch, size_t capacity)
                         { restored = job->attempt->resume(job->remote, request, outgoing.request, scratch, capacity, id, revision,
                                                           outgoing.state == 4 ? outgoing.terminal_data : gc::ByteView{}); });
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
        ++epoch;
    };
    if (job.draft_stage == Stage::Binding)
    {
        const auto result = s.store->stepCommit();
        if (result == JournalWriteResult::InProgress) return;
        if (result != JournalWriteResult::Verified)
        {
            stop("Author binding could not be saved");
            return;
        }
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
    if (!s.state->view().find(4, key, value) || !gc::storage::decodeDraft(key, value, draft) || draft.generation != job.draft_generation)
    {
        stop("Draft changed; review before publishing");
        return;
    }
    if (job.draft_stage == Stage::Bind)
    {
        const bool unbound = !draft.author.size;
        auto* scratch = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.author", value.size + 80, false));
        if (!scratch)
        {
            stop("Insufficient author workspace");
            return;
        }
        const auto result = s.store->bindDraftAuthor(key, job.draft_generation, {job.author.data(), job.author.size()}, scratch, value.size + 80);
        heap_caps_free(scratch);
        if (result != JournalWriteResult::InProgress && result != JournalWriteResult::Verified)
        {
            stop("Author binding rejected");
            return;
        }
        if (unbound) ++job.draft_generation;
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
    job.unsigned_bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.record", capacity, false));
    job.size = capacity + 70;
    job.bytes = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.sign", 2 * job.size + 26, false));
    gc::GeocacheId cache;
    gc::RevisionHash hash;
    if (!job.unsigned_bytes || !job.bytes || !gc::protocol::encodeGeocacheRecord(record, job.unsigned_bytes, capacity, job.unsigned_size) ||
        gc::protocol::deriveGeocacheHashes({job.unsigned_bytes, job.unsigned_size}, s.crypto, job.bytes, job.size, cache, hash) != gc::protocol::VerificationResult::Valid)
    {
        stop("Cannot prepare signed record");
        return;
    }
    std::array<uint8_t, 36> issued_key{};
    std::memcpy(issued_key.data(), cache.bytes.data(), 32);
    issued_key[35] = 1;
    gc::ByteView previous;
    if (s.state->view().find(3, {issued_key.data(), issued_key.size()}, previous))
    {
        gc::storage::AuthorIssuedView issued;
        if (!gc::storage::decodeAuthorIssued({issued_key.data(), issued_key.size()}, previous, issued) || !issued.issued_at.has_utc)
        {
            stop("Previous issuance requires recovery");
            return;
        }
        record.created_at = issued.issued_at.utc_seconds;
        auto latest = issued;
        size_t cursor = 0;
        gc::storage::MutationView row;
        const auto view = s.state->view();
        while (view.next(cursor, row))
        {
            if (row.table != 3 || row.key.size != 36 || std::memcmp(row.key.data, cache.bytes.data(), 32)) continue;
            gc::storage::AuthorIssuedView candidate;
            if (!gc::storage::decodeAuthorIssued(row.key, row.value, candidate) || !candidate.issued_at.has_utc ||
                std::memcmp(candidate.author_public_key.data, job.author.data(), 64))
            {
                stop("Author history requires recovery");
                return;
            }
            if (candidate.revision > latest.revision) latest = candidate;
        }
        record.revision = latest.revision;
        record.updated_at = latest.issued_at.utc_seconds;
        if (record.revision > 1)
        {
            for (unsigned i = 0; i < 4; ++i) issued_key[32 + i] = static_cast<uint8_t>((record.revision - 1) >> ((3 - i) * 8));
            if (!view.find(3, {issued_key.data(), issued_key.size()}, previous) ||
                !gc::storage::decodeAuthorIssued({issued_key.data(), issued_key.size()}, previous, issued))
            {
                stop("Predecessor history missing");
                return;
            }
            record.previous_hash = issued.revision_hash;
        }
        if (!gc::protocol::encodeGeocacheRecord(record, job.unsigned_bytes, capacity, job.unsigned_size) ||
            gc::protocol::deriveGeocacheHashes({job.unsigned_bytes, job.unsigned_size}, s.crypto, job.bytes, job.size, cache, hash) != gc::protocol::VerificationResult::Valid)
        {
            stop("Cannot reconstruct latest version");
            return;
        }
        if (!std::memcmp(hash.bytes.data(), latest.revision_hash.data, 32)) job.issued = latest.issued_at;
        else
        {
            const auto published = gc::storage::draftPublication(view, key, draft);
            if (published.confirmed_revision != latest.revision)
            {
                stop("Previous version unconfirmed; resolve it first");
                return;
            }
            if (latest.revision == UINT32_MAX || job.issued.utc_seconds < latest.issued_at.utc_seconds)
            {
                stop("Version limit or clock regression");
                return;
            }
            record.revision = latest.revision + 1;
            record.previous_hash = latest.revision_hash;
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
    gc::ByteView value;
    gc::storage::DraftView draft;
    gc::Destination remote;
    const auto utc = std::time(nullptr);
    const bool ready = session && session->phase == Phase::Ready && session->port && session->store &&
                       !session->store->needsRecovery() && !session->store->commitPending() && !session->client->persistencePending() &&
                       !downloadActive() && !publicationActive() && !draftSaveActive() && utc >= 946684800 &&
                       static_cast<uint64_t>(utc) <= 253402300799ULL && session->port->pageSource(remote) &&
                       session->state->view().find(4, {id.data(), id.size()}, value) && gc::storage::decodeDraft({id.data(), id.size()}, value, draft) &&
                       draft.generation == generation && draft.has_coordinates &&
                       gc::protocol::validRecordText(draft.name, false, true) && router->getGeocachingAuthorKey(author.data()) &&
                       (!draft.author.size || !std::memcmp(draft.author.data, author.data(), author.size()));
    if (!ready) return false;
    const auto history = gc::storage::draftPublication(session->state->view(), {id.data(), id.size()}, draft);
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
            std::snprintf(out.status.data(), out.status.size(), "Updating...");
            return;
        }
        if (section == ::ui::geocaching::Section::Published && session && session->store && !session->store->needsRecovery())
        {
            size_t cursor = 0;
            gc::storage::MutationView row;
            const auto view = session->state->view();
            while (view.next(cursor, row))
                if (row.table == 4) ++out.count;
            out.generation = session->store->committedSequence();
            out.can_create = !session->store->commitPending() && !downloadActive() && !publicationActive() && !draftSaveActive() &&
                             storage::sd_card_ready() && !storage::sd_external_block_owner_active();
            std::snprintf(out.status.data(), out.status.size(), "%s", out.count ? "Drafts and saved publication status" : "No local drafts");
        }
        else if (section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->store->needsRecovery())
            session->saved->snapshot(out);
        else if (session && session->source && session->phase == Phase::Ready) session->source->snapshot(section, out);
        else
        {
            std::snprintf(out.status.data(), out.status.size(), "%s", session ? session->status : "Starting Geocaching...");
            out.can_refresh = session && session->phase == Phase::Failed;
        }
        if (session && session->notice) std::snprintf(out.status.data(), out.status.size(), "%s", session->notice);
        if (downloadActive())
        {
            out.can_refresh = false;
            out.has_more = false;
            const auto phase = session->download->phase();
            std::snprintf(out.status.data(), out.status.size(), "%s", phase == gc::DownloadPhase::Waiting ? "Downloading cache..." : phase == gc::DownloadPhase::Cancelling ? "Cancelling download..."
                                                                                                                                                                            : "Saving and verifying GPX...");
        }
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
        out.generation ^= epoch << 32;
    }
    bool item(::ui::geocaching::Section section, size_t index, uint64_t generation, ::ui::geocaching::Item& out) override
    {
        Guard guard;
        if (guard.locked && section == ::ui::geocaching::Section::Published && session && session->store && !session->store->needsRecovery())
        {
            out = {};
            if ((generation ^ (epoch << 32)) != session->store->committedSequence()) return false;
            size_t cursor = 0;
            gc::storage::MutationView row;
            const auto view = session->state->view();
            while (view.next(cursor, row))
            {
                if (row.table != 4 || index--) continue;
                gc::storage::DraftView draft;
                if (!gc::storage::decodeDraft(row.key, row.value, draft)) return false;
                out.is_draft = true;
                out.edit_generation = draft.generation;
                std::memcpy(out.id.data(), row.key.data, 16);
                if (draft.name.empty()) std::snprintf(out.name.data(), out.name.size(), "Untitled draft");
                else std::memcpy(out.name.data(), draft.name.data(), draft.name.size());
                out.latitude_e7 = draft.latitude_e7;
                out.longitude_e7 = draft.longitude_e7;
                std::snprintf(out.detail.data(), out.detail.size(), "Local draft - not published\n%s\n%s", draft.has_coordinates ? "Location set" : "Location not set", draft.author.size ? "Author selected" : "Author not selected");
                const auto publication = gc::storage::draftPublication(view, row.key, draft);
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
            return false;
        }
        if (guard.locked && section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->store->needsRecovery())
            return session->saved->item(index, generation ^ (epoch << 32), out);
        if (!guard.locked || !session || !session->source || !session->source->item(section, index, generation ^ (epoch << 32), out)) return false;
        out.downloaded = session->saved && session->saved->contains(out.id, out.revision_hash);
        out.can_download = !out.downloaded && !downloadActive() && !publicationActive() && !draftSaveActive() && !session->store->commitPending() &&
                           (!session->saved || !session->saved->pending()) &&
                           !session->store->needsRecovery() && storage::sd_card_ready() &&
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
        if (section == ::ui::geocaching::Section::Downloaded && session && session->saved && !session->store->needsRecovery()) session->saved->reset();
        else if (session && session->phase == Phase::Failed) restart.store(true);
        else if (session && session->source) session->source->refresh(section);
        next_step.store(0);
    }
    bool loadMore() override
    {
        Guard guard;
        const bool begun = guard.locked && !downloadActive() && !publicationActive() && !draftSaveActive() && session && session->source && session->source->loadMore();
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
        if (!sink || !session || !session->store || session->store->needsRecovery()) return Status::Failed;
        if (cancel_draft_read.exchange(false)) session->draft_read.reset();
        if (!session->draft_read || session->draft_read->id != id)
        {
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
        gc::ByteView bytes;
        gc::storage::DraftView draft;
        if (!session->state->view().find(4, {id.data(), id.size()}, bytes) ||
            !gc::storage::decodeDraft({id.data(), id.size()}, bytes, draft) || draft.generation != session->draft_read->generation)
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
            if (session && session->draft_read && session->draft_read->id == id) session->draft_read.reset();
        }
        else cancel_draft_read.store(true);
        next_step.store(0);
    }
    bool saveDraft(::ui::geocaching::DraftInput& input) override
    {
        Guard guard;
        if (!guard.locked || !session || !session->store || session->store->needsRecovery() || session->store->commitPending() ||
            downloadActive() || publicationActive() || draftSaveActive() || session->phase == Phase::ResumeDownloads ||
            (session->client && session->client->persistencePending()) || input.generation == UINT64_MAX ||
            input.name.size() > 96 || input.description.size() > 2048 || input.hint.size() > 512) return false;
        gc::storage::DraftView draft;
        if (input.generation)
        {
            gc::ByteView stored;
            if (!session->state->view().find(4, {input.id.data(), input.id.size()}, stored) ||
                !gc::storage::decodeDraft({input.id.data(), input.id.size()}, stored, draft) || draft.generation != input.generation) return false;
        }
        else if (input.id == std::array<uint8_t, 16>{}) esp_fill_random(input.id.data(), input.id.size());
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
        if (!guard.locked || !session || !session->source || !session->port || downloadActive() || publicationActive() || draftSaveActive() || session->store->commitPending()) return false;
        if (session->client->phase() != gc::QueryClientPhase::PageReady && session->client->phase() != gc::QueryClientPhase::Failed) return false;
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
        const auto install_generation = session->store->nextDownloadGeneration(summary.id);
        if (!install_generation) return false;
        std::array<uint8_t, 16> task;
        gc::RequestId request;
        randomId(nullptr, task.data());
        randomId(nullptr, request.bytes.data());
        session->download.reset();
        session->download_port.reset();
        session->download_port.reset(new (std::nothrow) SdDownloadPort<Digest>(*session->store, *session->state, session->crypto,
                                                                               session->local, {summary.id, summary.hash, install_generation}, task, now(nullptr)));
        if (!session->download_port) return false;
        session->download.reset(new (std::nothrow) gc::DownloadClient(*session->download_port, session->crypto));
        if (!session->download || !session->download->begin(remote, request, summary, install_generation))
        {
            session->download.reset();
            session->download_port.reset();
            return false;
        }
        session->download_started = now(nullptr).monotonic_ms;
        session->download_wait_ms = gc::QueryClient::kReplyTimeoutMs;
        session->download_scratch = summary.signed_bytes + 64;
        if (session->saved) session->saved->reset();
        ++epoch;
        next_step.store(0);
        return true;
    }
} facade;

bool closeSession()
{
    if (!session) return true;
    if (session->client && session->store && !session->store->needsRecovery())
    {
        if (session->client->persistencePending())
        {
            session->client->tick(now(nullptr).monotonic_ms);
            return false;
        }
        if (session->store->commitPending())
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
    if (!guard.locked || !id || !session || !session->store || session->store->commitPending() || session->store->needsRecovery() ||
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
        !session->port || !session->store || session->store->commitPending() || session->store->needsRecovery() ||
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
    if (!wanted.load()) return active.load();
    return static_cast<int32_t>(millis() - next_step.load()) >= 0;
}
void step()
{
    Guard guard;
    if (!guard.locked || !router || !board) return;
    next_step.store(millis() + 5);
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
    if (cancel_draft_read.exchange(false)) s.draft_read.reset();
    if (!storage::sd_card_ready() || storage::sd_external_block_owner_active())
    {
        if (!s.notice)
        {
            s.notice = "SD card unavailable";
            if (s.saved) s.saved->reset();
            ++epoch;
        }
        next_step.store(millis() + 1000);
        if (s.draft_read) s.draft_read->status = ::ui::geocaching::DraftReadStatus::Failed;
        return;
    }
    if (s.notice)
    {
        s.notice = nullptr;
        ++epoch;
    }
    if (s.draft_read && s.draft_read->status == ::ui::geocaching::DraftReadStatus::Pending)
    {
        auto& job = *s.draft_read;
        gc::ByteView bytes;
        gc::storage::DraftView draft;
        const bool valid = s.store && !s.store->needsRecovery() && s.state &&
                           s.state->view().find(4, {job.id.data(), job.id.size()}, bytes) &&
                           gc::storage::decodeDraft({job.id.data(), job.id.size()}, bytes, draft);
        job.status = valid ? ::ui::geocaching::DraftReadStatus::Ready : ::ui::geocaching::DraftReadStatus::Failed;
        if (valid) job.generation = draft.generation;
        return;
    }
    if (draftSaveActive())
    {
        auto& job = *s.draft_save;
        const auto result = job.started ? s.store->stepCommit() : s.store->saveDraft({job.id.data(), job.id.size()}, {job.bytes, job.size}, job.expected);
        job.started = true;
        heap_caps_free(job.bytes);
        job.bytes = nullptr;
        if (result != JournalWriteResult::InProgress)
        {
            job.done = true;
            job.saved = result == JournalWriteResult::Verified;
            ++epoch;
        }
        return;
    }
    if (s.saved && s.saved->pending() && s.phase != Phase::ResumeDownloads && !downloadActive() && !publicationActive() && !s.store->commitPending() && !s.store->needsRecovery())
    {
        s.saved->advance();
        return;
    }
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
        const auto result = s.recovery->step(s.frame, kFrameCapacity, s.mutations.data(), s.mutations.size(), recoveredStateValid);
        if (result == StateRecoveryStep::Working) return;
        if (result != StateRecoveryStep::JournalRestored)
        {
            fail("Cached state needs recovery or more workspace");
            return;
        }
        s.store.reset(new (std::nothrow) SdRequestStore(s.volume, s.recovery->replayedSequence(), *s.state));
        s.recovery.reset();
        heap_caps_free(s.frame);
        s.frame = nullptr;
        if (!s.store)
        {
            fail("Insufficient memory");
            return;
        }
        s.saved.reset(new (std::nothrow) SavedCacheCatalog<Digest>(*s.state, s.crypto));
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
            if (result != gc::DownloadOperationResult::Complete)
            {
                fail("Downloaded GPX recovery needs attention");
                return;
            }
            s.download_port.reset();
            s.saved->reset();
            ++epoch;
            return;
        }
        const auto view = s.state->view();
        size_t cursor = 0;
        gc::storage::MutationView row;
        while (view.next(cursor, row))
        {
            if (row.table != 5) continue;
            gc::storage::OutgoingView outgoing;
            gc::storage::TaskView task;
            gc::ByteView value;
            if (!gc::storage::decodeOutgoing(row.key, row.value, outgoing) || outgoing.state != 4 ||
                !outgoing.continue_intent || !view.find(10, outgoing.task_id, value) ||
                !gc::storage::decodeTask(outgoing.task_id, value, task) || task.kind != 2 ||
                task.state == 3 || task.state == 5 || !task.continue_intent ||
                task.cache_id.size != 32 || task.revision_hash.size != 32 ||
                !gc::storage::requestBelongsToTask(outgoing.task_id, task, row.key, outgoing) ||
                !s.store->downloadIntentActive(row.key, outgoing.install_generation)) continue;
            gc::Destination local, remote;
            gc::RequestId request;
            gc::InstallIdentity identity;
            std::array<uint8_t, 16> task_id;
            std::memcpy(local.bytes.data(), row.key.data, 16);
            std::memcpy(remote.bytes.data(), row.key.data + 16, 16);
            std::memcpy(request.bytes.data(), row.key.data + 32, 16);
            std::memcpy(task_id.data(), outgoing.task_id.data, 16);
            std::memcpy(identity.id.bytes.data(), task.cache_id.data, 32);
            std::memcpy(identity.hash.bytes.data(), task.revision_hash.data, 32);
            identity.generation = outgoing.install_generation;
            s.download_port.reset(new (std::nothrow) SdDownloadPort<Digest>(*s.store, *s.state, s.crypto,
                                                                            local, identity, task_id, outgoing.created));
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
        s.port.reset(new (std::nothrow) QueryStorePort(*s.store, *s.state, local, randomId, now, nullptr));
        s.local = local;
        if (!s.port)
        {
            fail("Insufficient memory");
            return;
        }
        s.client.reset(new (std::nothrow) gc::QueryClient(*s.port));
        s.dispatcher.reset(new (std::nothrow) RequestDispatcher(*router, *s.store, 5000, 120000));
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
    if (s.store->needsRecovery())
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
                if (restored == PublicationRestore::Restored) return;
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
        if (restored == PublicationRestore::Restored) return;
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
    if (s.store->commitPending())
    {
        s.dispatcher->dispatchOne(now(nullptr));
        return;
    }
    if (s.response)
    {
        if (s.response_operation == 1 && s.publication && s.publication->attempt)
        {
            s.publication->attempt->accept(s.response_source, {s.response, s.response_size});
            ++epoch;
        }
        else if (s.response_operation == 3 && s.download)
        {
            auto* scratch = static_cast<uint8_t*>(mem::allocatePreferred("geocaching.verify", s.download_scratch, false));
            if (scratch)
            {
                const auto before = s.download->phase();
                s.download->accept(s.response_source, {s.response, s.response_size}, scratch, s.download_scratch);
                if (before != s.download->phase()) ++epoch;
                heap_caps_free(scratch);
            }
        }
        else s.client->accept(s.response_source, {s.response, s.response_size});
        heap_caps_free(s.response);
        s.response = nullptr;
        s.response_size = 0;
        return;
    }
    if (s.download && s.download->phase() == gc::DownloadPhase::Waiting &&
        now(nullptr).monotonic_ms - s.download_started >= s.download_wait_ms)
    {
        s.download->cancel();
        ++epoch;
        return;
    }
    if (s.announcement)
    {
        const auto& incoming = *s.announcement;
        s.client->observe(incoming.discovery, incoming.delivery, {incoming.key.data(), incoming.key.size()},
                          {incoming.data.data(), incoming.size}, now(nullptr).monotonic_ms);
        s.announcement.reset();
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
    const auto sent = s.dispatcher->dispatchOne(now(nullptr));
    if (sent.status == DispatchStatus::StorageBlocked || sent.status == DispatchStatus::Corrupt)
        fail("Query storage is blocked");
    else if (!s.store->commitPending()) next_step.store(millis() + 250);
}
} // namespace platform::esp::arduino_common::geocaching::browse_runtime
