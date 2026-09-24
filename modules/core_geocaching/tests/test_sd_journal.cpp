#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/query_request.h"
#include "geocaching/protocol/record_encoder.h"
#include "geocaching/storage/download_recovery.h"
#include "geocaching/storage/draft_publication.h"
#include "geocaching/storage/task_record.h"
#include "platform/esp/arduino_common/geocaching/query_browse_source.h"
#include "platform/esp/arduino_common/geocaching/query_store_port.h"
#include "platform/esp/arduino_common/geocaching/request_dispatcher.h"
#include "platform/esp/arduino_common/geocaching/saved_cache_catalog.h"
#include "platform/esp/arduino_common/geocaching/sd_author_issue_port.h"
#include "platform/esp/arduino_common/geocaching/sd_download_port.h"
#include "platform/esp/arduino_common/geocaching/sd_gpx_stage.h"
#include "platform/esp/arduino_common/geocaching/sd_journal.h"
#include "platform/esp/arduino_common/geocaching/sd_publish_port.h"
#include "platform/esp/arduino_common/geocaching/sd_request_store.h"
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fixture
{
std::map<std::string, std::string> files;
bool flush_ok = true, ready = true, busy = false;
size_t write_limit = SIZE_MAX, corrupt_at = SIZE_MAX;
unsigned writes = 0, step_data_calls = 0, step_io_calls = 0;
size_t step_data_bytes = 0;
bool supported_volume = true;
::geocaching::storage::VolumeInstance volume{};
} // namespace fixture
namespace platform::esp::arduino_common::storage
{
SdFileReadResult sd_read_file(const char*, uint8_t* buffer, size_t capacity)
{
    ++fixture::step_io_calls;
    ++fixture::step_data_calls;
    fixture::step_data_bytes += 28;
    auto header = ::geocaching::storage::encodeVolumeHeader(fixture::volume);
    if (!fixture::supported_volume) header[5] = 2;
    SdFileReadResult result;
    result.status = SdFileReadStatus::Ready;
    result.file_size = header.size();
    result.bytes_read = std::min(capacity, header.size());
    std::memcpy(buffer, header.data(), result.bytes_read);
    return result;
}
class SdRuntimeFile::Impl
{
  public:
    std::string path;
    size_t position = 0;
    bool opened = false;
};
SdRuntimeFile::SdRuntimeFile() : impl_(new Impl) {}
SdRuntimeFile::~SdRuntimeFile() { delete impl_; }
bool SdRuntimeFile::open(const char* path, const char* mode)
{
    ++fixture::step_io_calls;
    impl_->path = path;
    impl_->position = 0;
    if (*mode == 'w') fixture::files[path].clear();
    impl_->opened = fixture::files.count(path) != 0;
    return impl_->opened;
}
void SdRuntimeFile::close()
{
    ++fixture::step_io_calls;
    impl_->opened = false;
}
bool SdRuntimeFile::is_open() const { return impl_->opened; }
size_t SdRuntimeFile::write(const void* bytes, size_t count)
{
    ++fixture::writes;
    ++fixture::step_io_calls;
    ++fixture::step_data_calls;
    fixture::step_data_bytes += count;
    const auto n = std::min(count, fixture::write_limit);
    fixture::files[impl_->path].append(static_cast<const char*>(bytes), n);
    return n;
}
bool SdRuntimeFile::flush()
{
    ++fixture::step_io_calls;
    return fixture::flush_ok;
}
uint64_t SdRuntimeFile::size() const
{
    ++fixture::step_io_calls;
    return fixture::files[impl_->path].size();
}
int SdRuntimeFile::read(void* bytes, size_t count)
{
    ++fixture::step_io_calls;
    ++fixture::step_data_calls;
    fixture::step_data_bytes += count;
    const auto& data = fixture::files[impl_->path];
    const auto n = std::min(count, data.size() - impl_->position);
    std::memcpy(bytes, data.data() + impl_->position, n);
    if (fixture::corrupt_at >= impl_->position && fixture::corrupt_at - impl_->position < n)
        static_cast<uint8_t*>(bytes)[fixture::corrupt_at - impl_->position] ^= 1;
    impl_->position += n;
    return static_cast<int>(n);
}
bool sd_card_ready() { return fixture::ready; }
bool sd_external_block_owner_active() { return fixture::busy; }
bool sd_is_directory(const char*)
{
    ++fixture::step_io_calls;
    return true;
}
bool sd_exists(const char* path)
{
    ++fixture::step_io_calls;
    return fixture::files.count(path) != 0;
}
bool sd_rename(const char* from, const char* to)
{
    ++fixture::step_io_calls;
    if (!fixture::files.count(from) || fixture::files.count(to)) return false;
    fixture::files[to] = std::move(fixture::files[from]);
    fixture::files.erase(from);
    return true;
}
} // namespace platform::esp::arduino_common::storage
struct NativeRecordCrypto final : ::geocaching::protocol::RecordCrypto
{
    bool sha256(::geocaching::ByteView bytes, uint8_t out[32]) override
    {
        ::chat::reticulum::fullHash(bytes.data, bytes.size, out);
        return true;
    }
    ::geocaching::protocol::VerificationResult verifyEd25519(::geocaching::ByteView key, ::geocaching::ByteView signature, ::geocaching::ByteView message) override
    {
        return ed25519_verify(signature.data, message.data, message.size, key.data) ? ::geocaching::protocol::VerificationResult::Valid : ::geocaching::protocol::VerificationResult::InvalidSignature;
    }
};
// Test driver only: production callers return to their owner after each step.
using namespace platform::esp::arduino_common::geocaching;
bool stepBudgetOk() { return fixture::step_io_calls <= 1 && fixture::step_data_calls <= 1 && fixture::step_data_bytes <= 512; }
JournalWriteResult drainJournal(SdGeocachingJournal& journal, JournalWriteResult result)
{
    for (unsigned i = 0; result == JournalWriteResult::InProgress && i < 1024; ++i)
    {
        fixture::step_io_calls = fixture::step_data_calls = 0;
        fixture::step_data_bytes = 0;
        result = journal.step();
        if (!stepBudgetOk()) return JournalWriteResult::Invalid;
    }
    return result;
}
JournalWriteResult drainStore(SdRequestStore& store, JournalWriteResult result)
{
    const auto sequence = store.committedSequence();
    for (unsigned i = 0; result == JournalWriteResult::InProgress && i < 1024; ++i)
    {
        fixture::step_io_calls = fixture::step_data_calls = 0;
        fixture::step_data_bytes = 0;
        result = store.stepCommit();
        if (!stepBudgetOk() || (result == JournalWriteResult::InProgress && store.committedSequence() != sequence))
            return JournalWriteResult::Invalid;
    }
    return result;
}
int checkIncrementalBoundaries()
{
    // Host fixture deliberately exceeds one I/O slice.
    std::array<uint8_t, 4096> bytes{};
    const uint8_t key = 1;
    ::geocaching::storage::MutationView mutation{5, {&key, 1}, {bytes.data(), bytes.size()}, false};
    unsigned completed_steps = 0;
    {
        fixture::files.clear();
        auto journal = std::make_unique<SdGeocachingJournal>(fixture::volume);
        fixture::step_io_calls = 0;
        auto result = journal->begin(0, &mutation, 1);
        if (result != JournalWriteResult::InProgress || fixture::step_io_calls) return 90;
        if (journal->begin(1, &mutation, 1) != JournalWriteResult::Busy) return 91;
        while (result == JournalWriteResult::InProgress && completed_steps < 1024)
        {
            fixture::step_io_calls = fixture::step_data_calls = 0;
            fixture::step_data_bytes = 0;
            result = journal->step();
            ++completed_steps;
            if (!stepBudgetOk()) return 92;
        }
        if (result != JournalWriteResult::Verified || completed_steps < 30) return 93;
    }
    // Cancel at every pre-completion boundary, including open, flush and readback.
    for (unsigned boundary = 0; boundary < completed_steps; ++boundary)
    {
        fixture::files.clear();
        auto journal = std::make_unique<SdGeocachingJournal>(fixture::volume);
        auto result = journal->begin(0, &mutation, 1);
        for (unsigned i = 0; i < boundary; ++i) result = journal->step();
        if (result != JournalWriteResult::InProgress) return 94;
        fixture::step_io_calls = 0;
        if (journal->cancel() != JournalWriteResult::Cancelled || fixture::step_io_calls > 1 ||
            journal->mayHaveWritten() != !fixture::files.empty()) return 95;
        fixture::step_io_calls = 0;
        if (journal->step() != JournalWriteResult::Cancelled || fixture::step_io_calls) return 96;
    }
    fixture::files.clear();
    uint8_t first[1024]{}, second[1024]{};
    ::geocaching::storage::LogicalState state(first, second, sizeof(first));
    auto store = std::make_unique<SdRequestStore>(fixture::volume, 0, state);
    const auto start = [&]()
    {
        uint8_t request[128]{};
        ::geocaching::RequestId id;
        std::array<uint8_t, 16> task{};
        size_t size = 0;
        if (!::geocaching::protocol::encodeCapabilitiesRequest(id, request, sizeof(request), size))
            return JournalWriteResult::Invalid;
        const auto result = store->persistNewTask({}, {}, id, task, 3, {request, size}, {});
        // All caller-owned bytes expire before the first storage step.
        std::memset(request, 0xa5, sizeof(request));
        task.fill(0xa5);
        return result;
    };
    if (start() != JournalWriteResult::InProgress || state.view().size() || store->committedSequence() ||
        !fixture::files.empty() || state.beginSnapshot()) return 97;
    if (start() != JournalWriteResult::Busy || state.view().size()) return 98;
    if (store->cancelCommit() != JournalWriteResult::Cancelled || store->needsRecovery() ||
        store->commitPending() || state.view().size()) return 99;
    if (start() != JournalWriteResult::InProgress) return 100;
    unsigned steps = 0;
    auto result = JournalWriteResult::InProgress;
    while (result == JournalWriteResult::InProgress && steps++ < 1024)
    {
        result = store->stepCommit();
        if (result == JournalWriteResult::InProgress && (state.view().size() || store->committedSequence())) return 101;
    }
    if (result != JournalWriteResult::Verified || state.view().size() != 2 || store->committedSequence() != 1) return 102;
    fixture::files.clear();
    return 0;
}

int checkIncrementalDispatch()
{
    class DelayedStore : public SdRequestStore
    {
      public:
        using SdRequestStore::SdRequestStore;
        unsigned selection_wait = 0, send_wait = 0, expiration_calls = 0;
        DispatchReadResult readPending(const ::geocaching::Destination& local, ::geocaching::ByteView after,
                                       ::geocaching::storage::PendingRequestView& out) override
        {
            if (selection_wait)
            {
                --selection_wait;
                out = {};
                return DispatchReadResult::Pending;
            }
            return SdRequestStore::readPending(local, after, out);
        }
        DispatchReadResult readForSend(::geocaching::ByteView key, DispatchSendView& out) override
        {
            if (send_wait)
            {
                --send_wait;
                out = {};
                return DispatchReadResult::Pending;
            }
            return SdRequestStore::readForSend(key, out);
        }
        JournalWriteResult expireOneAttempt(const ::geocaching::storage::StoredTime& now, uint64_t started, uint64_t timeout, bool& expired) override
        {
            ++expiration_calls;
            return SdRequestStore::expireOneAttempt(now, started, timeout, expired);
        }
    };
    for (unsigned scenario = 0; scenario < 8; ++scenario)
    {
        const auto mode = scenario % 4;
        fixture::files.clear();
        fixture::flush_ok = true;
        uint8_t first[2048]{}, second[2048]{}, request[128]{};
        ::geocaching::storage::LogicalState state(first, second, sizeof(first));
        auto store = std::make_unique<DelayedStore>(fixture::volume, 0, state);
        store->selection_wait = store->send_wait = scenario >= 4 ? 3 : 0;
        ::geocaching::RequestId id;
        size_t size = 0;
        if (!::geocaching::protocol::encodeCapabilitiesRequest(id, request, sizeof(request), size) ||
            drainStore(*store, store->persistNewTask({}, {}, id, {}, 3, {request, size}, {})) != JournalWriteResult::Verified) return 110;
        chat::MeshAdapterRouter router;
        router.send_ok = mode != 1;
        auto dispatcher = std::make_unique<RequestDispatcher>(router, *store, 100, 1000);
        if (mode == 2) fixture::flush_ok = false;
        bool stopped = false, done = false;
        for (unsigned step = 0; step < 300; ++step)
        {
            // Stop after attempt reservation commits but before sending. The
            // real dispatcher must reacquire state and honor the task intent.
            if (mode == 3 && !stopped && store->committedSequence() == 2 && !store->commitPending())
            {
                if (drainStore(*store, store->stopTask({})) != JournalWriteResult::Verified) return 111;
                stopped = true;
            }
            fixture::step_io_calls = fixture::step_data_calls = 0;
            fixture::step_data_bytes = 0;
            const auto old_sends = router.sends;
            const auto result = dispatcher->dispatchOne({});
            if (store->selection_wait && (router.sends || store->expiration_calls != 1)) return 240;
            if (store->send_wait && router.sends) return 241;
            if (!stepBudgetOk()) return 112;
            if (router.sends != old_sends && store->committedSequence() != 2) return 113;
            if (mode == 0 && result.status == DispatchStatus::Submitted)
            {
                if (router.sends != 1 || store->committedSequence() != 3 ||
                    router.sent_bytes.size() != size || std::memcmp(router.sent_bytes.data(), request, size)) return 114;
                done = true;
                break;
            }
            if (mode == 1 && store->committedSequence() == 3 && !store->commitPending())
            {
                if (router.sends != 1 || result.status != DispatchStatus::Deferred) return 115;
                dispatcher->dispatchOne({});
                if (router.sends != 1) return 116;
                done = true;
                break;
            }
            if (mode == 2 && result.status == DispatchStatus::StorageBlocked)
            {
                if (router.sends || !store->needsRecovery() || store->committedSequence() != 1) return 117;
                done = true;
                break;
            }
            if (mode == 3 && store->committedSequence() == 4 && !store->commitPending())
            {
                if (router.sends || !stopped) return 118;
                done = true;
                break;
            }
        }
        if (!done) return 119;
    }
    fixture::files.clear();
    fixture::flush_ok = true;
    return 0;
}

int checkBrowseFlow(const char* capabilities_path, const char* query_path)
{
    using namespace ::geocaching;
    fixture::files.clear();
    std::ifstream caps_file(capabilities_path, std::ios::binary), query_file(query_path, std::ios::binary);
    std::vector<uint8_t> caps((std::istreambuf_iterator<char>(caps_file)), {});
    std::vector<uint8_t> response((std::istreambuf_iterator<char>(query_file)), {});
    std::array<uint8_t, 4096> first{}, second{};
    storage::LogicalState state(first.data(), second.data(), first.size());
    auto store = std::make_unique<SdRequestStore>(fixture::volume, 0, state);
    unsigned sequence = 0;
    QueryStorePort port(
        *store, state, {}, [](void* context, uint8_t out[16])
        {
        std::memset(out, ++*static_cast<unsigned*>(context), 16); return true; },
        [](void*)
        { return storage::StoredTime{}; },
        &sequence);
    auto client = std::make_unique<QueryClient>(port);
    QueryBrowseSource source(*client, port, {300000000, 1200000000, 310000000, 1210000000});
    const auto advance = [&]()
    {
        for (unsigned i = 0; i < 1024; ++i)
        {
            const auto phase = client->phase();
            if (phase != QueryClientPhase::PersistingRequest && phase != QueryClientPhase::PersistingCapabilities &&
                phase != QueryClientPhase::PersistingPage) return phase != QueryClientPhase::Failed;
            fixture::step_io_calls = fixture::step_data_calls = 0;
            fixture::step_data_bytes = 0;
            client->tick(0);
            if (!stepBudgetOk()) return false;
        }
        return false;
    };
    std::array<uint8_t, 64> key{};
    std::vector<uint8_t> announce{0x95, 1, 0xc4, 16};
    announce.insert(announce.end(), 16, 0);
    announce.insert(announce.end(), {0xc4, 16});
    announce.insert(announce.end(), 16, 0);
    announce.insert(announce.end(), {0, 0xa1, 'x'});
    if (!client->observe({}, {}, {key.data(), key.size()}, {announce.data(), announce.size()}, 0)) return 120;
    source.refresh(::ui::geocaching::Section::Discover);
    if (!client->tick(0) || client->phase() != QueryClientPhase::PersistingRequest || !advance() ||
        client->phase() != QueryClientPhase::CheckingCapabilities) return 121;
    if (client->accept({}, {caps.data(), caps.size()}) || client->phase() != QueryClientPhase::PersistingCapabilities ||
        !advance() || !client->tick(1) || !advance() || client->phase() != QueryClientPhase::Querying) return 122;
    ::ui::geocaching::Snapshot snapshot;
    source.snapshot(::ui::geocaching::Section::Discover, snapshot);
    if (snapshot.count || client->accept({}, {response.data(), response.size()}) ||
        client->phase() != QueryClientPhase::PersistingPage) return 123;
    source.snapshot(::ui::geocaching::Section::Discover, snapshot);
    RequestId id;
    id.bytes.fill(3);
    if (snapshot.count || port.accepted({}, id, {response.data(), response.size()})) return 124;
    const auto original = response;
    std::fill(response.begin(), response.end(), 0xa5);
    if (!advance() || client->phase() != QueryClientPhase::PageReady) return 125;
    source.snapshot(::ui::geocaching::Section::Discover, snapshot);
    ::ui::geocaching::Item item;
    if (snapshot.count != 1 || !source.item(::ui::geocaching::Section::Discover, 0, snapshot.generation, item) ||
        std::strcmp(item.name.data(), "Test") || !std::strstr(item.detail.data(), "Directory preview") ||
        !port.accepted({}, id, {original.data(), original.size()})) return 126;
    Destination wrong;
    wrong.bytes[0] = 1;
    if (port.accepted(wrong, id, {original.data(), original.size()})) return 127;
    source.refresh(::ui::geocaching::Section::Discover);
    if (source.item(::ui::geocaching::Section::Discover, 0, snapshot.generation, item)) return 128;
    if (!client->tick(10) || !advance() || client->phase() != QueryClientPhase::Querying) return 129;
    client->tick(10);
    if (!client->tick(10 + QueryClient::kReplyTimeoutMs) || client->phase() != QueryClientPhase::Cancelling) return 130;
    id.bytes.fill(5);
    auto late = original;
    protocol::CmpReader late_reader({late.data(), late.size()});
    size_t late_fields = 0;
    uint64_t late_value = 0;
    ByteView late_id;
    if (!late_reader.array(late_fields, 6) || !late_reader.unsignedInteger(late_value) ||
        !late_reader.unsignedInteger(late_value) || !late_reader.unsignedInteger(late_value) ||
        !late_reader.binary(late_id, 16)) return 135;
    std::memcpy(late.data() + (late_id.data - late.data()), id.bytes.data(), 16);
    protocol::QueryPageView late_page;
    if (!protocol::decodeQueryPage({late.data(), late.size()}, id, 2048, 20, late_page)) return 136;
    if (port.accepted({}, id, {late.data(), late.size()})) return 131;
    for (unsigned i = 0; client->persistencePending() && i < 1024; ++i) client->tick(10 + QueryClient::kReplyTimeoutMs);
    if (client->phase() != QueryClientPhase::Failed || client->failure() != QueryFailure::Timeout ||
        !port.accepted({}, id, {late.data(), late.size()})) return 132;
    storage::PendingRequestView pending;
    if (storage::nextPendingRequest(state.view(), {}, {}, pending) != storage::PendingRequestResult::None) return 133;
    source.snapshot(::ui::geocaching::Section::Discover, snapshot);
    if (!snapshot.can_refresh || !std::strstr(snapshot.status.data(), "did not reply")) return 134;
    fixture::files.clear();
    return 0;
}

int checkDownloadReceipt(const char* query_path, const char* response_path)
{
    using namespace ::geocaching;
    fixture::files.clear();
    std::ifstream query_file(query_path, std::ios::binary), reply_file(response_path, std::ios::binary);
    std::vector<uint8_t> query((std::istreambuf_iterator<char>(query_file)), {}), reply((std::istreambuf_iterator<char>(reply_file)), {});
    RequestId id;
    id.bytes.fill(3);
    protocol::SummaryView summary;
    protocol::QueryPageView page;
    if (!protocol::decodeQueryResponse({query.data(), query.size()}, id, 2048, &summary, 1, page)) return 140;
    id.bytes.fill(4);
    uint8_t request[128]{};
    size_t request_size = 0;
    if (!protocol::encodeGetRequest(id, summary.id, &summary.hash, nullptr, 8192, request, sizeof(request), request_size)) return 141;
    std::array<uint8_t, 4096> first{}, second{};
    storage::LogicalState state(first.data(), second.data(), first.size());
    auto store = std::make_unique<SdRequestStore>(fixture::volume, 0, state);
    storage::RequestTaskTarget target{{summary.id.bytes.data(), 32}, {summary.hash.bytes.data(), 32}, 1};
    std::array<uint8_t, 16> task_id{};
    if (store->nextDownloadGeneration(summary.id) != 1 ||
        store->persistNewTask({}, {}, id, task_id, 2, {request, request_size}, {}, target) != JournalWriteResult::InProgress ||
        store->nextDownloadGeneration(summary.id) != 1) return 142;
    if (drainStore(*store, JournalWriteResult::InProgress) != JournalWriteResult::Verified ||
        store->nextDownloadGeneration(summary.id) != 2) return 143;
    NativeRecordCrypto crypto;
    if (store->recordDownloadResponse({}, {}, id, 2, {reply.data(), reply.size()}, crypto) != JournalWriteResult::StateRejected) return 144;
    protocol::GetResponseView parsed;
    if (!protocol::decodeGetResponse({reply.data(), reply.size()}, id, 8192, parsed)) return 145;
    protocol::CmpReader signed_record(parsed.signed_cache);
    size_t count = 0;
    ByteView record, signature;
    if (!signed_record.array(count, 2) || !signed_record.binary(record, 4096) || !signed_record.binary(signature, 64)) return 146;
    auto corrupt = reply;
    corrupt[signature.data - reply.data()] ^= 1;
    if (store->recordDownloadResponse({}, {}, id, 1, {corrupt.data(), corrupt.size()}, crypto) != JournalWriteResult::Invalid ||
        store->committedSequence() != 1) return 147;
    if (drainStore(*store, store->recordDownloadResponse({}, {}, id, 1, {reply.data(), reply.size()}, crypto)) != JournalWriteResult::Verified) return 148;
    ByteView bytes;
    storage::TaskView task;
    storage::CacheHeadView head;
    if (!state.view().find(10, {task_id.data(), task_id.size()}, bytes) || !storage::decodeTask({task_id.data(), task_id.size()}, bytes, task) ||
        task.state != 1 || !state.view().find(2, target.cache_id, bytes) || !storage::decodeCacheHead(target.cache_id, bytes, head) ||
        head.current_hash.size) return 149; // Received is not Installed.
    const auto committed = store->committedSequence();
    // Use the durably recorded response as the source for actual GPX staging.
    uint8_t request_key[48]{};
    std::memcpy(request_key + 32, id.bytes.data(), 16);
    storage::OutgoingView outgoing;
    if (!state.view().find(5, {request_key, 48}, bytes) || !storage::decodeOutgoing({request_key, 48}, bytes, outgoing) ||
        !protocol::decodeGetResponse(outgoing.terminal_data, id, 8192, parsed)) return 155;
    protocol::VerifiedRecordView verified;
    auto verification = protocol::VerificationResult::WorkspaceTooSmall;
    state.withScratch([&](uint8_t* scratch, size_t capacity)
                      { verification = protocol::verifyGeocache(parsed.signed_cache, crypto, scratch, capacity, verified, &summary.id, &summary.hash); });
    if (verification != protocol::VerificationResult::Valid) return 156;
    std::array<uint8_t, 16> transaction{};
    transaction[0] = 7;
    auto gpx = std::make_unique<SdGpxStage>();
    auto written = gpx->begin(transaction, verified, crypto);
    for (unsigned i = 0; written == StageResult::InProgress && i < 512; ++i)
    {
        fixture::step_io_calls = fixture::step_data_calls = 0;
        fixture::step_data_bytes = 0;
        written = gpx->step();
        if (!stepBudgetOk()) return 157;
    }
    if (written != StageResult::Written || fixture::files.at(gpx->path()).find("<name>Test</name>") == std::string::npos ||
        fixture::files.at(gpx->path()).find("<tm:signature") == std::string::npos) return 158;
    if (store->recordDownloadResponse({}, {}, id, 1, {reply.data(), reply.size()}, crypto) != JournalWriteResult::Verified ||
        store->committedSequence() != committed) return 150;
    std::array<uint8_t, 32> file_hash{};
    const auto& staged = fixture::files.at(gpx->path());
    crypto.sha256({reinterpret_cast<const uint8_t*>(staged.data()), staged.size()}, file_hash.data());
    if (store->finishDownloadInstall({request_key, 48}, transaction, 1, file_hash) != JournalWriteResult::StateRejected) return 159;
    if (drainStore(*store, store->prepareDownloadInstall({request_key, 48}, transaction, 1, file_hash, {}, crypto)) != JournalWriteResult::Verified) return 160;
    const auto prepared_sequence = store->committedSequence();
    if (store->prepareDownloadInstall({request_key, 48}, transaction, 1, file_hash, {}, crypto) != JournalWriteResult::Verified ||
        store->committedSequence() != prepared_sequence) return 161;
    auto wrong_hash = file_hash;
    wrong_hash[0] ^= 1;
    if (store->finishDownloadInstall({request_key, 48}, transaction, 1, wrong_hash) != JournalWriteResult::StateRejected ||
        store->finishDownloadInstall({request_key, 48}, transaction, 2, file_hash) != JournalWriteResult::StateRejected) return 162;
    std::string target_path = "/trailmate/geocaching/caches/";
    constexpr char alphabet[] = "0123456789abcdef";
    for (auto byte : summary.id.bytes)
    {
        target_path += alphabet[byte >> 4];
        target_path += alphabet[byte & 15];
    }
    target_path += ".gpx";
    if (!platform::esp::arduino_common::storage::sd_rename(gpx->path(), target_path.c_str())) return 163;
    const auto& installed = fixture::files.at(target_path);
    crypto.sha256({reinterpret_cast<const uint8_t*>(installed.data()), installed.size()}, file_hash.data());
    if (store->finishDownloadInstall({request_key, 48}, transaction, 1, file_hash) != JournalWriteResult::InProgress) return 164;
    if (!state.view().find(10, {task_id.data(), task_id.size()}, bytes) || !storage::decodeTask({task_id.data(), task_id.size()}, bytes, task) ||
        task.state != 1) return 165;
    if (drainStore(*store, JournalWriteResult::InProgress) != JournalWriteResult::Verified) return 166;
    storage::InstallRecordView install;
    if (!state.view().find(10, {task_id.data(), task_id.size()}, bytes) || !storage::decodeTask({task_id.data(), task_id.size()}, bytes, task) ||
        task.state != 3 || !state.view().find(2, target.cache_id, bytes) || !storage::decodeCacheHead(target.cache_id, bytes, head) ||
        head.current_hash.size != 32 || std::memcmp(head.current_hash.data, summary.hash.bytes.data(), 32) ||
        !state.view().find(12, {transaction.data(), transaction.size()}, bytes) ||
        !storage::decodeInstallRecord({transaction.data(), transaction.size()}, bytes, install) || install.phase != storage::InstallPhase::Installed) return 167;
    const auto finished_sequence = store->committedSequence();
    if (store->finishDownloadInstall({request_key, 48}, transaction, 1, file_hash) != JournalWriteResult::Verified ||
        store->committedSequence() != finished_sequence) return 168;
    id.bytes.fill(5);
    task_id[0] = 1;
    if (!protocol::encodeGetRequest(id, summary.id, &summary.hash, nullptr, 8192, request, sizeof(request), request_size)) return 151;
    if (store->persistNewTask({}, {}, id, task_id, 2, {request, request_size}, {}, target) != JournalWriteResult::StateRejected) return 152;
    target.install_generation = 2;
    if (drainStore(*store, store->persistNewTask({}, {}, id, task_id, 2, {request, request_size}, {}, target)) != JournalWriteResult::Verified) return 153;
    id.bytes.fill(4);
    if (store->recordDownloadResponse({}, {}, id, 1, {reply.data(), reply.size()}, crypto) != JournalWriteResult::StateRejected) return 154;
    fixture::files.clear();
    return 0;
}

struct FileDigest
{
    std::vector<uint8_t> bytes;
    void update(const uint8_t* data, size_t size) { bytes.insert(bytes.end(), data, data + size); }
    bool finalize(uint8_t* out, size_t)
    {
        chat::reticulum::fullHash(bytes.data(), bytes.size(), out);
        return true;
    }
};
int checkDownloadController(const char* query_path, const char* response_path)
{
    using namespace ::geocaching;
    std::ifstream q(query_path, std::ios::binary), r(response_path, std::ios::binary);
    std::vector<uint8_t> query((std::istreambuf_iterator<char>(q)), {}), response((std::istreambuf_iterator<char>(r)), {});
    RequestId id;
    id.bytes.fill(3);
    protocol::SummaryView summary;
    protocol::QueryPageView page;
    if (!protocol::decodeQueryResponse({query.data(), query.size()}, id, 2048, &summary, 1, page)) return 170;
    id.bytes.fill(4);
    std::string target = "/trailmate/geocaching/caches/";
    constexpr char hex[] = "0123456789abcdef";
    for (auto byte : summary.id.bytes)
    {
        target += hex[byte >> 4];
        target += hex[byte & 15];
    }
    target += ".gpx";
    for (unsigned scenario = 0; scenario < 5; ++scenario)
    {
        fixture::files.clear();
        if (scenario == 2) fixture::files[target] = "user-owned file";
        std::array<uint8_t, 4096> first{}, second{};
        storage::LogicalState state(first.data(), second.data(), first.size());
        auto store = std::make_unique<SdRequestStore>(fixture::volume, 0, state);
        NativeRecordCrypto crypto;
        std::array<uint8_t, 16> task;
        task.fill(0x42);
        auto checkRecovery = [&](const std::array<uint8_t, 16>& task, const RequestId& id, uint64_t generation) -> int
        {
            // Power cut at every quiescent storage boundary. Rebuild all
            // logical state from journal bytes, not from the live arena.
            const auto disk = fixture::files;
            std::array<uint8_t, 4096> recovered_a{}, recovered_b{};
            storage::LogicalState recovered(recovered_a.data(), recovered_b.data(), recovered_a.size());
            uint64_t sequence = 0;
            for (const auto& file : disk)
            {
                if (file.first.rfind("/trailmate/geocaching/.state/journal/", 0) != 0) continue;
                storage::RecordFrameView frame;
                storage::TransactionView transaction;
                storage::MutationView mutations[3];
                if (!storage::decodeRecordFrame({reinterpret_cast<const uint8_t*>(file.second.data()), file.second.size()}, frame) ||
                    frame.sequence != sequence + 1 || !storage::decodeTransaction(frame.payload, sequence, mutations, 3, transaction) ||
                    !recovered.apply(transaction.mutations, transaction.count, [](const auto& candidate)
                                     { return storage::validateTaskReferences(candidate) && storage::validateAttemptReferences(candidate); })) return 190;
                sequence = frame.sequence;
            }
            storage::DownloadRecoveryRequest selected;
            SdRequestStore recovered_store(fixture::volume, sequence, recovered);
            LogicalDownloadStore recovered_downloads(recovered_store, recovered);
            DownloadStore& downloads = recovered_downloads;
            if (downloads.readRecovery({}, selected) != DownloadRecoveryRead::Ready ||
                selected.task != task || selected.identity.generation != generation ||
                std::memcmp(selected.key.data() + 32, id.bytes.data(), 16)) return 191;
            {
                SdDownloadPort<FileDigest> recovered_port(downloads, crypto, {}, selected.identity, selected.task, selected.created);
                auto result = recovered_port.resume({}, id);
                for (unsigned attempt = 0; result == DownloadOperationResult::Pending && attempt < 512; ++attempt)
                {
                    fixture::step_data_bytes = 0;
                    result = recovered_port.poll();
                    if (fixture::step_data_bytes > 512) return 192;
                }
                if (result != DownloadOperationResult::Complete || !fixture::files.count(target) ||
                    fixture::files.at(target).find("<name>Test</name>") == std::string::npos) return 193;
            }
            const auto after = selected.key;
            if (downloads.readRecovery({after.data(), after.size()}, selected) != DownloadRecoveryRead::End) return 244;
            fixture::files = disk;
            return 0;
        };
        SdDownloadPort<FileDigest> port(*store, state, crypto, {}, {summary.id, summary.hash, 1}, task, {});
        DownloadClient client(port, crypto);
        LogicalDownloadStore generation_store(*store, state);
        uint64_t next_generation = 0;
        if (generation_store.readNextGeneration(summary.id, next_generation) != DownloadRecoveryRead::Ready || next_generation != 1) return 257;
        if (!client.begin({}, id, summary, 1)) return 171;
        if (generation_store.readNextGeneration(summary.id, next_generation) != DownloadRecoveryRead::Busy || next_generation) return 258;
        for (unsigned i = 0; client.phase() == DownloadPhase::Submitting && i < 256; ++i) client.advance();
        if (client.phase() != DownloadPhase::Waiting) return 172;
        if (generation_store.readNextGeneration(summary.id, next_generation) != DownloadRecoveryRead::Ready || next_generation != 2) return 259;
        if (scenario == 4)
        {
            RequestId query_id;
            query_id.bytes.fill(3);
            std::array<uint8_t, 16> query_task;
            query_task.fill(0x71);
            uint8_t query_request[256];
            size_t query_size = 0;
            if (!protocol::encodeQueryRequest(query_id, {-900000000, -1800000000, 900000000, 1800000000},
                                              7, {}, 4, {}, 8192, query_request, sizeof(query_request), query_size) ||
                drainStore(*store, store->persistNewTask({}, {}, query_id, query_task, 3, {query_request, query_size}, {})) != JournalWriteResult::Verified ||
                drainStore(*store, store->commitQueryResult({}, {}, query_id, {query.data(), query.size()})) != JournalWriteResult::Verified) return 255;
            LogicalDownloadStore downloads(*store, state);
            storage::DownloadRecoveryRequest selected;
            protocol::SummaryView selected_preview;
            if (downloads.readWaitingDownload({}, selected, selected_preview) != DownloadRecoveryRead::Ready ||
                selected.task != task || selected.identity.generation != 1 || selected_preview.name != summary.name) return 256;
            const auto disk = fixture::files;
            const auto sequence = store->committedSequence();
            SdDownloadPort<FileDigest> resumed_port(downloads, crypto, {}, selected.identity, selected.task, selected.created);
            DownloadClient resumed(resumed_port, crypto);
            if (resumed_port.resumeWaiting({}, id) != DownloadOperationResult::Complete ||
                !resumed.resume({}, id, selected_preview, 1) || resumed.phase() != DownloadPhase::Waiting ||
                store->committedSequence() != sequence || store->commitPending() || fixture::files != disk) return 194;
            downloads.releaseRead();
            Destination wrong_source;
            wrong_source.bytes.fill(0x99);
            std::vector<uint8_t> scratch(summary.signed_bytes + 64);
            if (resumed.accept(wrong_source, {response.data(), response.size()}, scratch.data(), scratch.size()) ||
                resumed.phase() != DownloadPhase::Waiting || store->commitPending()) return 195;
            if (resumed.accept({}, {response.data(), response.size()}, scratch.data(), scratch.size()) ||
                resumed.phase() != DownloadPhase::Installing) return 196;
            for (unsigned i = 0; resumed.phase() == DownloadPhase::Installing && i < 512; ++i) resumed.advance();
            if (resumed.phase() != DownloadPhase::Stored || !fixture::files.count(target) ||
                fixture::files.at(target).find("<name>Test</name>") == std::string::npos) return 197;
            continue;
        }
        if (scenario == 1)
        {
            if (!client.cancel()) return 173;
            for (unsigned i = 0; client.phase() == DownloadPhase::Cancelling && i < 256; ++i) client.advance();
            if (client.phase() != DownloadPhase::Cancelled || fixture::files.count(target)) return 174;
            continue;
        }
        bool accepted = false;
        state.withScratch([&](uint8_t* bytes, size_t capacity)
                          { accepted = client.accept({}, {response.data(), response.size()}, bytes, capacity); });
        if (accepted || client.phase() != DownloadPhase::Installing) return 175;
        for (unsigned i = 0; client.phase() == DownloadPhase::Installing && i < 512; ++i)
        {
            fixture::step_data_bytes = 0;
            client.advance();
            if (fixture::step_data_bytes > 512) return 176;
            if (scenario == 0 && client.phase() == DownloadPhase::Installing && !store->commitPending())
            {
                if (const int result = checkRecovery(task, id, 1)) return result;
            }
        }
        if (scenario == 2)
        {
            if (client.phase() != DownloadPhase::Failed || fixture::files.at(target) != "user-owned file") return 177;
        }
        else if (client.phase() != DownloadPhase::Stored || fixture::files.at(target).find("<name>Test</name>") == std::string::npos) return 178;
        if (scenario == 0)
        {
            LogicalDownloadStore catalog_store(*store, state);
            SavedCacheCatalog<FileDigest> saved(catalog_store, crypto);
            ::ui::geocaching::Snapshot snapshot;
            saved.snapshot(snapshot);
            if (snapshot.count) return 184;
            for (unsigned i = 0; saved.pending() && i < 256; ++i) saved.advance();
            saved.snapshot(snapshot);
            ::ui::geocaching::Item item;
            fixture::step_io_calls = 0;
            if (snapshot.count != 1 || !saved.item(0, snapshot.generation, item) || !item.downloaded ||
                std::strcmp(item.name.data(), "Test") || fixture::step_io_calls) return 185;
            // The projection owns only the requested rows. A window change
            // preserves the total so the UI does not jump back to page zero.
            const auto old_generation = snapshot.generation;
            saved.requestWindow(1, 1);
            saved.snapshot(snapshot);
            if (snapshot.count != 1 || saved.item(0, old_generation, item) || fixture::step_io_calls) return 341;
            for (unsigned i = 0; saved.pending() && i < 256; ++i) saved.advance();
            saved.snapshot(snapshot);
            if (snapshot.count != 1 || saved.item(1, snapshot.generation, item)) return 342;
            // Reset during hashing must be a UI-only notification. The next
            // owner step discards the old reader and starts the new window.
            saved.requestWindow(0, 1);
            saved.advance();
            fixture::step_io_calls = 0;
            saved.reset();
            if (fixture::step_io_calls || saved.contains(summary.id.bytes, summary.hash.bytes)) return 343;
            for (unsigned i = 0; saved.pending() && i < 256; ++i) saved.advance();
            auto missing = summary.id.bytes;
            missing[0] ^= 1;
            fixture::step_io_calls = 0;
            if (!saved.requestPreview(1, 0, 2)) return 344;
            saved.previewRow(0, summary.id.bytes, summary.hash.bytes);
            saved.previewRow(1, missing, summary.hash.bytes);
            if (saved.checked(missing, summary.hash.bytes) || fixture::step_io_calls) return 345;
            for (unsigned i = 0; saved.pending() && i < 256; ++i) saved.advance();
            fixture::step_io_calls = 0;
            if (!saved.checked(missing, summary.hash.bytes) || saved.contains(missing, summary.hash.bytes) ||
                !saved.contains(summary.id.bytes, summary.hash.bytes) || saved.requestPreview(1, 0, 2) || fixture::step_io_calls) return 346;
            if (!saved.requestPreview(2, 0, 1)) return 347;
            saved.previewRow(0, missing, summary.hash.bytes);
            if (saved.checked(missing, summary.hash.bytes) || saved.contains(summary.id.bytes, summary.hash.bytes)) return 348;
            saved.requestWindow(0, 4);
            const auto valid_file = fixture::files[target];
            fixture::files[target][0] ^= 1;
            saved.reset();
            for (unsigned i = 0; saved.pending() && i < 256; ++i) saved.advance();
            saved.snapshot(snapshot);
            if (snapshot.count || !std::strstr(snapshot.status.data(), "changed")) return 186;
            fixture::files[target] = valid_file;
            std::array<uint8_t, 4096> restored_a{}, restored_b{};
            storage::LogicalState restored(restored_a.data(), restored_b.data(), restored_a.size());
            uint64_t replayed = 0;
            for (const auto& file : fixture::files)
            {
                if (file.first.rfind("/trailmate/geocaching/.state/journal/", 0) != 0) continue;
                storage::RecordFrameView frame;
                storage::TransactionView transaction;
                storage::MutationView mutations[3];
                if (!storage::decodeRecordFrame({reinterpret_cast<const uint8_t*>(file.second.data()), file.second.size()}, frame) ||
                    frame.sequence != replayed + 1 || !storage::decodeTransaction(frame.payload, replayed, mutations, 3, transaction) ||
                    !restored.apply(transaction.mutations, transaction.count, [](const auto& candidate)
                                    { return storage::validateTaskReferences(candidate) && storage::validateAttemptReferences(candidate); })) return 188;
                replayed = frame.sequence;
            }
            if (replayed != store->committedSequence()) return 189;
            SdRequestStore restored_store(fixture::volume, replayed, restored);
            LogicalDownloadStore restored_catalog_store(restored_store, restored);
            SavedCacheCatalog<FileDigest> reopened(restored_catalog_store, crypto);
            for (unsigned i = 0; reopened.pending() && i < 256; ++i) reopened.advance();
            reopened.snapshot(snapshot);
            if (snapshot.count != 1) return 187;
            // More than 64 heads before the real installed cache. The larger
            // host-only ledger is fixture input, never a device allocation.
            std::vector<uint8_t> many_a(16384), many_b(16384);
            storage::LogicalState many(many_a.data(), many_b.data(), many_a.size());
            for (unsigned i = 0; i < 70; ++i)
            {
                std::array<uint8_t, 32> key{};
                key[0] = uint8_t(i);
                if (key == summary.id.bytes) return 349;
                storage::CacheHeadView empty_head;
                empty_head.install_generation = 1;
                uint8_t value[128];
                size_t size = 0;
                if (!storage::encodeCacheHead({key.data(), key.size()}, empty_head, value, sizeof(value), size)) return 350;
                storage::MutationView mutation{2, {key.data(), key.size()}, {value, size}, false};
                if (!many.apply(&mutation, 1, [](const auto&)
                                { return true; })) return 351;
            }
            size_t cursor = 0;
            storage::MutationView row;
            while (restored.view().next(cursor, row))
                if (!many.apply(&row, 1, [](const auto&)
                                { return true; })) return 352;
            SdRequestStore many_store(fixture::volume, replayed, many);
            LogicalDownloadStore many_catalog_store(many_store, many);
            SavedCacheCatalog<FileDigest> expanded(many_catalog_store, crypto);
            for (unsigned i = 0; expanded.pending() && i < 512; ++i) expanded.advance();
            expanded.snapshot(snapshot);
            if (expanded.pending() || snapshot.count != 1 || !expanded.item(0, snapshot.generation, item)) return 353;
            // UI getters do not touch the ledger after projection. Invalidation
            // belongs to its serialized owner when mutations are published.
            storage::MutationView erase{2, {summary.id.bytes.data(), 32}, {}, true};
            if (!many.apply(&erase, 1, [](const auto&)
                            { return true; })) return 354;
            fixture::step_io_calls = 0;
            if (!expanded.item(0, snapshot.generation, item) || !expanded.contains(summary.id.bytes, summary.hash.bytes) ||
                std::strcmp(item.name.data(), "Test") || fixture::step_io_calls) return 355;
            expanded.reset();
            for (unsigned i = 0; expanded.pending() && i < 512; ++i) expanded.advance();
            expanded.snapshot(snapshot);
            if (snapshot.count || expanded.contains(summary.id.bytes, summary.hash.bytes)) return 356;
        }
        if (scenario == 0 || scenario == 3)
        {
            if (scenario == 3) fixture::files[target] = "external edit";
            RequestId next_id;
            next_id.bytes.fill(5);
            auto next_response = response;
            protocol::CmpReader envelope({next_response.data(), next_response.size()});
            size_t fields = 0;
            uint64_t value = 0;
            ByteView encoded_id;
            if (!envelope.array(fields, 6) || !envelope.unsignedInteger(value) || !envelope.unsignedInteger(value) ||
                !envelope.unsignedInteger(value) || !envelope.binary(encoded_id, 16)) return 179;
            std::memcpy(next_response.data() + (encoded_id.data - next_response.data()), next_id.bytes.data(), 16);
            std::array<uint8_t, 16> next_task;
            next_task.fill(0x43);
            SdDownloadPort<FileDigest> update_port(*store, state, crypto, {}, {summary.id, summary.hash, 2}, next_task, {});
            DownloadClient update(update_port, crypto);
            if (!update.begin({}, next_id, summary, 2)) return 180;
            for (unsigned i = 0; update.phase() == DownloadPhase::Submitting && i < 256; ++i) update.advance();
            std::vector<uint8_t> scratch(summary.signed_bytes + 64);
            if (update.accept({}, {next_response.data(), next_response.size()}, scratch.data(), scratch.size()) ||
                update.phase() != DownloadPhase::Installing) return 181;
            for (unsigned i = 0; update.phase() == DownloadPhase::Installing && i < 512; ++i)
            {
                update.advance();
                if (scenario == 0 && update.phase() == DownloadPhase::Installing && !store->commitPending())
                    if (const int result = checkRecovery(next_task, next_id, 2)) return result;
            }
            if (scenario == 3)
            {
                if (update.phase() != DownloadPhase::Failed || fixture::files.at(target) != "external edit") return 182;
            }
            else
            {
                std::string history = "/trailmate/geocaching/.state/history/";
                for (auto byte : summary.hash.bytes)
                {
                    history += hex[byte >> 4];
                    history += hex[byte & 15];
                }
                history += ".gpx";
                if (update.phase() != DownloadPhase::Stored || update_port.historyPending() || !fixture::files.count(history)) return 183;
            }
        }
    }
    fixture::files.clear();
    return 0;
}

int checkDraftPersistence()
{
    using namespace ::geocaching;
    fixture::files.clear();
    std::array<uint8_t, 4096> first{}, second{};
    storage::LogicalState state(first.data(), second.data(), first.size());
    SdRequestStore store(fixture::volume, 0, state);
    std::array<uint8_t, 16> id{};
    const ByteView key{id.data(), id.size()};
    storage::DraftView draft;
    uint8_t encoded[256];
    size_t size = 0;
    ByteView stored;
    if (!storage::encodeDraft(key, draft, encoded, sizeof(encoded), size) ||
        store.saveDraft(key, {encoded, size}, 0) != JournalWriteResult::InProgress ||
        state.view().find(4, key, stored)) return 198;
    std::memset(encoded, 0xa5, sizeof(encoded)); // candidate owns submitted bytes
    if (drainStore(store, JournalWriteResult::InProgress) != JournalWriteResult::Verified ||
        !state.view().find(4, key, stored) || !storage::decodeDraft(key, stored, draft) ||
        draft.generation != 1 || draft.has_coordinates || !draft.name.empty()) return 199;
    draft.name = "New cache";
    draft.has_coordinates = true;
    draft.latitude_e7 = 305000000;
    draft.longitude_e7 = 1205000000;
    draft.generation = 2;
    if (!storage::encodeDraft(key, draft, encoded, sizeof(encoded), size) ||
        drainStore(store, store.saveDraft(key, {encoded, size}, 1)) != JournalWriteResult::Verified) return 200;
    if (store.saveDraft(key, {encoded, size}, 1) != JournalWriteResult::StateRejected || store.commitPending()) return 201;
    std::array<uint8_t, 64> author{}, other_author{};
    other_author[0] = 1;
    if (drainStore(store, store.bindDraftAuthor(key, 2, {author.data(), author.size()}, encoded, sizeof(encoded))) != JournalWriteResult::Verified ||
        !state.view().find(4, key, stored) || !storage::decodeDraft(key, stored, draft) || draft.generation != 3 || draft.author.size != 64) return 219;
    const auto sequence = store.committedSequence();
    storage::PublicationHistory history;
    GeocacheId cache;
    if (store.readPublicationHistory(key, 3, cache, {author.data(), author.size()}, history) != DraftReadResult::Ready ||
        history.latest_revision || history.confirmed_revision || store.committedSequence() != sequence ||
        store.readPublicationHistory(key, 2, cache, {author.data(), author.size()}, history) != DraftReadResult::NotFound ||
        store.readPublicationHistory(key, 3, cache, {other_author.data(), other_author.size()}, history) != DraftReadResult::NotFound ||
        store.needsRecovery()) return 225;
    if (store.bindDraftAuthor(key, 3, {author.data(), author.size()}, encoded, sizeof(encoded)) != JournalWriteResult::Verified ||
        store.committedSequence() != sequence || store.commitPending() ||
        store.bindDraftAuthor(key, 3, {other_author.data(), other_author.size()}, encoded, sizeof(encoded)) != JournalWriteResult::StateRejected ||
        store.bindDraftAuthor(key, 2, {author.data(), author.size()}, encoded, sizeof(encoded)) != JournalWriteResult::StateRejected) return 220;
    draft.author = {other_author.data(), other_author.size()};
    draft.generation = 4;
    if (!storage::encodeDraft(key, draft, encoded, sizeof(encoded), size) ||
        store.saveDraft(key, {encoded, size}, 3) != JournalWriteResult::StateRejected) return 221;
    draft.author = {};
    draft.name = "Edited without UI identity metadata";
    if (!storage::encodeDraft(key, draft, encoded, sizeof(encoded), size) ||
        store.editDraft(key, encoded, size, sizeof(encoded), 3) != JournalWriteResult::InProgress ||
        !store.inputConsumed() || store.readDraft(key, stored) != DraftReadResult::Busy) return 222;
    std::memset(encoded, 0xa5, sizeof(encoded));
    if (drainStore(store, JournalWriteResult::InProgress) != JournalWriteResult::Verified ||
        store.readDraft(key, stored) != DraftReadResult::Ready || !storage::decodeDraft(key, stored, draft) ||
        draft.generation != 4 || draft.name != "Edited without UI identity metadata" || draft.author.size != 64 ||
        std::memcmp(draft.author.data, author.data(), 64)) return 223;
    store.releaseDraftRead();
    NativeRecordCrypto catalog_crypto;
    storage::DraftCatalogPage catalog;
    if (store.readDraftCatalog(0, catalog_crypto, catalog) != DraftReadResult::Ready || catalog.total != 1 || catalog.count != 1 ||
        catalog.rows[0].generation != 4 || std::strcmp(catalog.rows[0].name.data(), "Edited without UI identity metadata") ||
        !catalog.rows[0].has_author || catalog.rows[0].publication.latest_revision ||
        store.readDraftCatalog(1, catalog_crypto, catalog) != DraftReadResult::Ready || catalog.total != 1 || catalog.count) return 224;
    draft.author = {author.data(), author.size()};
    draft.name = "Must not replace saved text";
    draft.generation = 5;
    if (!storage::encodeDraft(key, draft, encoded, sizeof(encoded), size)) return 202;
    fixture::flush_ok = false;
    const auto failed = drainStore(store, store.saveDraft(key, {encoded, size}, 4));
    fixture::flush_ok = true;
    if (failed == JournalWriteResult::Verified || !state.view().find(4, key, stored) ||
        !storage::decodeDraft(key, stored, draft) || draft.generation != 4 || draft.name != "Edited without UI identity metadata") return 203;
    fixture::files.clear();
    return 0;
}

int checkAuthorPort(const char* path)
{
    using namespace ::geocaching;
    std::ifstream file(path, std::ios::binary);
    const std::vector<uint8_t> signed_bytes((std::istreambuf_iterator<char>(file)), {});
    protocol::CmpReader reader({signed_bytes.data(), signed_bytes.size()});
    ByteView encoded, signature;
    size_t count = 0;
    RecordView record;
    if (!reader.array(count, 2) || count != 2 || !reader.binary(encoded, 4096) ||
        !reader.binary(signature, 64) || !protocol::decodeGeocacheRecord(encoded, record)) return 204;
    struct Transport
    {
        ByteView author, signed_record;
        SdRequestStore* store;
        unsigned signatures = 0;
        bool getGeocachingAuthorKey(uint8_t out[64])
        {
            std::memcpy(out, author.data, 64);
            return true;
        }
        bool signGeocachingRecord(ByteView, uint8_t*, size_t, uint8_t* out, size_t capacity, size_t& written)
        {
            ++signatures;
            if (store->committedSequence() != 1 || store->commitPending() || capacity < signed_record.size) return false;
            std::memcpy(out, signed_record.data, signed_record.size);
            written = signed_record.size;
            return true;
        }
    };
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        fixture::files.clear();
        std::array<uint8_t, 4096> first{}, second{};
        storage::LogicalState state(first.data(), second.data(), first.size());
        SdRequestStore store(fixture::volume, 0, state);
        NativeRecordCrypto crypto;
        Transport transport{record.author_public_key, {signed_bytes.data(), signed_bytes.size()}, &store};
        SdAuthorIssuePort<Transport> port(transport, store, crypto, {});
        AuthorIssue issue(port);
        std::vector<uint8_t> scratch(encoded.size + 70);
        if (!issue.begin(encoded, scratch.data(), scratch.size())) return 205;
        issue.advance();
        issue.advance();
        if (issue.phase() != AuthorIssuePhase::Reserve || transport.signatures || !store.commitPending()) return 206;
        if (scenario == 2)
        {
            issue.cancel();
            if (issue.phase() != AuthorIssuePhase::Cancelled || transport.signatures || store.commitPending()) return 207;
            continue;
        }
        fixture::flush_ok = scenario == 0;
        for (unsigned step = 0; step < 512 && issue.phase() != AuthorIssuePhase::Signed && issue.phase() != AuthorIssuePhase::Failed; ++step)
        {
            fixture::step_data_bytes = 0;
            issue.advance();
            if (fixture::step_data_bytes > 512) return 208;
        }
        fixture::flush_ok = true;
        if (scenario == 0 ? issue.phase() != AuthorIssuePhase::Signed || transport.signatures != 1
                          : issue.phase() != AuthorIssuePhase::Failed || transport.signatures != 0) return 209;
    }
    fixture::files.clear();
    return 0;
}

int checkPublishPort(const char* record_path, const char* response_path)
{
    using namespace ::geocaching;
    std::ifstream a(record_path, std::ios::binary), b(response_path, std::ios::binary);
    std::vector<uint8_t> record((std::istreambuf_iterator<char>(a)), {}), response((std::istreambuf_iterator<char>(b)), {});
    std::vector<uint8_t> workspace(record.size() + 26);
    NativeRecordCrypto crypto;
    protocol::VerifiedRecordView verified;
    if (protocol::verifyGeocache({record.data(), record.size()}, crypto, workspace.data(), workspace.size(), verified) !=
        protocol::VerificationResult::Valid) return 210;
    storage::DraftView draft;
    draft.author = verified.record.author_public_key;
    draft.has_coordinates = true;
    draft.latitude_e7 = verified.record.latitude_e7;
    draft.longitude_e7 = verified.record.longitude_e7;
    draft.name = verified.record.name;
    draft.description = verified.record.description;
    draft.hint = verified.record.hint;
    draft.state = static_cast<uint8_t>(verified.record.state);
    draft.difficulty_x2 = verified.record.difficulty_x2;
    draft.terrain_x2 = verified.record.terrain_x2;
    draft.container_size = static_cast<uint8_t>(verified.record.container_size);
    for (unsigned scenario = 0; scenario < 3; ++scenario)
    {
        fixture::files.clear();
        std::array<uint8_t, 4096> first{}, second{};
        storage::LogicalState state(first.data(), second.data(), first.size());
        SdRequestStore store(fixture::volume, 0, state);
        if (scenario == 2)
        {
            workspace.resize(record.size() + 256);
            const auto key = verified.record.creation_nonce;
            size_t size = 0;
            if (!storage::encodeDraft(key, draft, workspace.data(), workspace.size(), size) ||
                drainStore(store, store.saveDraft(key, {workspace.data(), size}, 0)) != JournalWriteResult::Verified) return 229;
            storage::StoredTime issued;
            issued.has_utc = true;
            issued.utc_seconds = verified.record.updated_at;
            const auto reserved = store.reserveDraftUnsignedRecord(key, 1, verified.record.encoded, crypto, workspace.data(), workspace.size(), issued);
            ByteView value;
            storage::DraftView frozen;
            if (reserved != JournalWriteResult::InProgress || !state.view().find(4, key, value) ||
                !storage::decodeDraft(key, value, frozen) || frozen.base_hash.size || frozen.generation != 1) return 230;
            if (drainStore(store, reserved) != JournalWriteResult::Verified || !state.view().find(4, key, value) ||
                !storage::decodeDraft(key, value, frozen) || frozen.base_hash.size != 32 || frozen.generation != 2) return 231;
            frozen.name = "Edited too early";
            frozen.generation = 3;
            if (!storage::encodeDraft(key, frozen, workspace.data(), workspace.size(), size) ||
                store.saveDraft(key, {workspace.data(), size}, 2) != JournalWriteResult::StateRejected) return 232;
            std::array<uint8_t, 16> task{};
            RequestId request;
            request.bytes.fill(2);
            SdPublishPort port(store, crypto, {}, verified.id, verified.hash, task, issued);
            PublishAttempt attempt(port, crypto);
            if (!attempt.begin({}, request, {record.data(), record.size()}, workspace.data(), workspace.size())) return 233;
            for (unsigned i = 0; i < 512 && attempt.phase() == PublishAttemptPhase::Submitting; ++i) attempt.advance();
            if (attempt.phase() != PublishAttemptPhase::Waiting || !state.view().find(4, key, value) ||
                !storage::decodeDraft(key, value, frozen) || !storage::draftPublication(state.view(), key, frozen).base_retained) return 234;
            frozen.name = "Editable after signed request retention";
            frozen.generation = 3;
            if (!storage::encodeDraft(key, frozen, workspace.data(), workspace.size(), size) ||
                drainStore(store, store.saveDraft(key, {workspace.data(), size}, 2)) != JournalWriteResult::Verified) return 235;
            RecordView successor = verified.record;
            successor.revision = 2;
            successor.name = "Editable after signed request retention";
            ++successor.updated_at;
            issued.utc_seconds = successor.updated_at;
            std::vector<uint8_t> next_record(record.size() + 128);
            std::array<uint8_t, 32> wrong_parent{};
            successor.previous_hash = {wrong_parent.data(), wrong_parent.size()};
            if (!protocol::encodeGeocacheRecord(successor, next_record.data(), next_record.size(), size) ||
                store.reserveDraftUnsignedRecord(key, 3, {next_record.data(), size}, crypto, workspace.data(), workspace.size(), issued) != JournalWriteResult::StateRejected) return 236;
            successor.previous_hash = {verified.hash.bytes.data(), verified.hash.bytes.size()};
            ++successor.created_at;
            if (!protocol::encodeGeocacheRecord(successor, next_record.data(), next_record.size(), size) ||
                store.reserveDraftUnsignedRecord(key, 3, {next_record.data(), size}, crypto, workspace.data(), workspace.size(), issued) != JournalWriteResult::StateRejected) return 237;
            --successor.created_at;
            if (!protocol::encodeGeocacheRecord(successor, next_record.data(), next_record.size(), size) ||
                drainStore(store, store.reserveDraftUnsignedRecord(key, 3, {next_record.data(), size}, crypto, workspace.data(), workspace.size(), issued)) != JournalWriteResult::Verified ||
                !state.view().find(4, key, value) || !storage::decodeDraft(key, value, frozen) || frozen.generation != 4 ||
                storage::draftPublication(state.view(), key, frozen).base_retained) return 238;
            const auto sequence = store.committedSequence();
            if (store.reserveDraftUnsignedRecord(key, 4, {next_record.data(), size}, crypto, workspace.data(), workspace.size(), issued) != JournalWriteResult::Verified ||
                store.committedSequence() != sequence) return 239;
            continue;
        }
        if (drainStore(store, store.reserveUnsignedRecord(verified.record.encoded, crypto, workspace.data(), workspace.size(), {})) !=
            JournalWriteResult::Verified) return 211;
        std::array<uint8_t, 16> task{};
        RequestId request;
        request.bytes.fill(2);
        SdPublishPort port(store, crypto, {}, verified.id, verified.hash, task, {});
        PublishAttempt attempt(port, crypto);
        if (!attempt.begin({}, request, {record.data(), record.size()}, workspace.data(), workspace.size())) return 212;
        std::fill(workspace.begin(), workspace.end(), 0xa5);
        if (scenario && !attempt.cancel()) return 213;
        for (unsigned i = 0; i < 512 && (attempt.phase() == PublishAttemptPhase::Submitting || attempt.phase() == PublishAttemptPhase::Cancelling); ++i)
        {
            fixture::step_data_bytes = 0;
            attempt.advance();
            if (fixture::step_data_bytes > 512) return 214;
        }
        if (scenario)
        {
            ByteView value;
            storage::TaskView stopped;
            if (attempt.phase() != PublishAttemptPhase::Cancelled || !state.view().find(10, {task.data(), task.size()}, value) ||
                !storage::decodeTask({task.data(), task.size()}, value, stopped) || stopped.state != 5 || stopped.continue_intent) return 215;
            const auto history = storage::draftPublication(state.view(), verified.record.creation_nonce, draft);
            if (history.latest_revision != 1 || history.confirmed_revision || !history.stopped || history.pending) return 225;
            storage::PublicationRecoveryFilter filter;
            storage::PublicationRecoveryView selected;
            if (store.readPublicationRecovery(filter, selected) != DraftReadResult::NotFound) return 240;
            continue;
        }
        if (attempt.phase() != PublishAttemptPhase::Waiting) return 216;
        const auto pending_history = storage::draftPublication(state.view(), verified.record.creation_nonce, draft);
        if (pending_history.latest_revision != 1 || pending_history.confirmed_revision || !pending_history.pending || pending_history.local_changes) return 226;
        chat::MeshAdapterRouter router;
        router.send_ok = true;
        RequestDispatcher dispatcher(router, store, 100, 1000);
        bool sent = false;
        for (unsigned i = 0; i < 512 && !sent; ++i)
            sent = dispatcher.dispatchOne({}).status == DispatchStatus::Submitted;
        if (!sent || router.sends != 1) return 217;
        storage::PublicationRecoveryFilter filter;
        storage::PublicationRecoveryView selected;
        if (store.readPublicationRecovery(filter, selected) != DraftReadResult::Ready || selected.confirmed ||
            selected.task != task || selected.cache.bytes != verified.id.bytes || selected.hash.bytes != verified.hash.bytes ||
            std::memcmp(selected.key.data() + 32, request.bytes.data(), 16)) return 222;
        const auto sequence = store.committedSequence();
        SdPublishPort restored_port(store, crypto, {}, verified.id, verified.hash, task, {});
        PublishAttempt restored(restored_port, crypto);
        if (!restored_port.attachRestoredRequest({}, request) ||
            !restored.resume({}, request, selected.request, workspace.data(), workspace.size(), selected.cache, selected.hash, selected.response) ||
            store.committedSequence() != sequence || store.commitPending() || router.sends != 1) return 223;
        store.releaseDraftRead();
        if (restored.accept({}, {response.data(), response.size()}) || restored.phase() != PublishAttemptPhase::Committing) return 224;
        for (unsigned i = 0; i < 512 && restored.phase() == PublishAttemptPhase::Committing; ++i) restored.advance();
        ByteView value;
        storage::TaskView completed;
        if (restored.phase() != PublishAttemptPhase::Confirmed || !state.view().find(10, {task.data(), task.size()}, value) ||
            !storage::decodeTask({task.data(), task.size()}, value, completed) || completed.state != 3) return 218;
        if (store.readPublicationRecovery(filter, selected) != DraftReadResult::NotFound) return 241;
        filter.has_cache = filter.has_hash = true;
        filter.cache = verified.id;
        filter.hash = verified.hash;
        if (store.readPublicationRecovery(filter, selected) != DraftReadResult::Ready || !selected.confirmed ||
            selected.response.size != response.size() || std::memcmp(selected.response.data, response.data(), response.size())) return 242;
        store.releaseDraftRead();
        const auto published_history = storage::draftPublication(state.view(), verified.record.creation_nonce, draft);
        if (published_history.latest_revision != 1 || published_history.confirmed_revision != 1 || published_history.local_changes) return 227;
        auto changed_draft = draft;
        changed_draft.name = "Edited after publication";
        const auto edited_history = storage::draftPublication(state.view(), verified.record.creation_nonce, changed_draft);
        if (!edited_history.local_changes || edited_history.confirmed_revision != 1) return 228;
    }
    fixture::files.clear();
    return 0;
}

int checkDownloadRecoveryCursor()
{
    using namespace ::geocaching;
    using namespace ::geocaching::storage;
    // Metadata-only selection: payload signatures and files are exercised by
    // checkDownloadController. Arrange requests out of key order and change
    // encoded row lengths between selections, as installation commits do.
    std::array<uint8_t, 8192> first{}, second{};
    LogicalState state(first.data(), second.data(), first.size());
    uint8_t request_bytes[128], outgoing_bytes[256], task_bytes[256], head_bytes[64], install_bytes[160];
    std::array<uint8_t, 48> key{};
    std::array<uint8_t, 16> task_id{};
    GeocacheId cache;
    RevisionHash hash;
    OutgoingView outgoing;
    TaskView task;
    const uint8_t terminal[] = {0x90};
    for (uint8_t number : {uint8_t(30), uint8_t(10), uint8_t(20)})
    {
        RequestId request;
        request.bytes.fill(number);
        task_id.fill(number);
        cache.bytes.fill(number);
        hash.bytes.fill(number + 1);
        size_t request_size = 0, outgoing_size = 0, task_size = 0, head_size = 0;
        if (!protocol::encodeGetRequest(request, cache, &hash, nullptr, 8192, request_bytes, sizeof(request_bytes), request_size) ||
            !describeNewRequestTask({}, {}, request, task_id, 2, {request_bytes, request_size}, {}, key, outgoing, task,
                                    {{cache.bytes.data(), 32}, {hash.bytes.data(), 32}, 1})) return 245;
        outgoing.state = 4;
        outgoing.terminal_data = {terminal, sizeof(terminal)};
        task.state = number == 20 ? 5 : 1;
        task.continue_intent = number != 20;
        CacheHeadView head;
        head.install_generation = 1;
        if (!encodeOutgoing({key.data(), key.size()}, outgoing, outgoing_bytes, sizeof(outgoing_bytes), outgoing_size) ||
            !encodeTask({task_id.data(), task_id.size()}, task, task_bytes, sizeof(task_bytes), task_size) ||
            !encodeCacheHead({cache.bytes.data(), 32}, head, head_bytes, sizeof(head_bytes), head_size)) return 246;
        const MutationView rows[] = {{5, {key.data(), key.size()}, {outgoing_bytes, outgoing_size}, false},
                                     {10, {task_id.data(), task_id.size()}, {task_bytes, task_size}, false},
                                     {2, {cache.bytes.data(), 32}, {head_bytes, head_size}, false}};
        if (!state.apply(rows, 3, [](const auto& view)
                         { return validateTaskReferences(view); })) return 247;
    }
    DownloadRecoveryRequest selected;
    if (nextDownloadRecovery(state.view(), {}, selected) != DownloadRecoverySelection::Found || selected.key[32] != 10 || selected.installed) return 248;
    const auto after = selected.key;
    ByteView value;
    if (!state.view().find(10, {selected.task.data(), selected.task.size()}, value) || !decodeTask({selected.task.data(), selected.task.size()}, value, task)) return 249;
    task.state = 3;
    CacheHeadView head;
    head.install_generation = 1;
    head.highest_seen_revision = 1;
    head.current_hash = {selected.identity.hash.bytes.data(), 32};
    InstallRecordView install{{selected.identity.id.bytes.data(), 32}, head.current_hash, head.current_hash, {}, 1, InstallPhase::Installed};
    size_t task_size = 0, head_size = 0, install_size = 0;
    if (!encodeTask({selected.task.data(), selected.task.size()}, task, task_bytes, sizeof(task_bytes), task_size) ||
        !encodeCacheHead(install.cache_id, head, head_bytes, sizeof(head_bytes), head_size) ||
        !encodeInstallRecord({selected.task.data(), selected.task.size()}, install, install_bytes, sizeof(install_bytes), install_size)) return 250;
    const MutationView completed[] = {{10, {selected.task.data(), selected.task.size()}, {task_bytes, task_size}, false},
                                      {2, install.cache_id, {head_bytes, head_size}, false},
                                      {12, {selected.task.data(), selected.task.size()}, {install_bytes, install_size}, false}};
    if (!state.apply(completed, 3, [](const auto& view)
                     { return validateTaskReferences(view); })) return 251;
    if (nextDownloadRecovery(state.view(), {}, selected) != DownloadRecoverySelection::Found || selected.key != after || !selected.installed) return 252;
    if (nextDownloadRecovery(state.view(), {after.data(), after.size()}, selected) != DownloadRecoverySelection::Found || selected.key[32] != 30) return 253;
    const auto last = selected.key;
    if (nextDownloadRecovery(state.view(), {last.data(), last.size()}, selected) != DownloadRecoverySelection::End) return 254;
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 6) return 22;
    if (const int result = checkDownloadRecoveryCursor()) return result;
    if (const int result = checkPublishPort(argv[1], argv[2])) return result;
    if (const int result = checkAuthorPort(argv[1])) return result;
    if (const int result = checkDraftPersistence()) return result;
    if (const int result = checkDownloadController(argv[4], argv[5])) return result;
    if (const int result = checkDownloadReceipt(argv[4], argv[5])) return result;
    if (const int result = checkBrowseFlow(argv[3], argv[4])) return result;
    if (const int result = checkIncrementalBoundaries()) return result;
    if (const int result = checkIncrementalDispatch()) return result;
    using namespace platform::esp::arduino_common::geocaching;
    const uint8_t key = 1;
    ::geocaching::storage::MutationView mutation{5, {&key, 1}, {}, true};
    uint8_t payload[64]{};
    size_t size = 0;
    if (!::geocaching::storage::encodeTransaction(0, &mutation, 1, payload, sizeof(payload), size)) return 1;
    auto journal = std::make_unique<SdGeocachingJournal>(fixture::volume);
    if (drainJournal(*journal, journal->begin(0, {payload, size})) != JournalWriteResult::Verified) return 2;
    const auto saved = fixture::files;
    // Cut power after every writer step. The authoritative journal must be
    // absent or byte-for-byte complete; an unpublished staging file is retryable.
    bool reached_verified = false;
    for (unsigned cut = 0; cut < 128 && !reached_verified; ++cut)
    {
        fixture::files.clear();
        auto interrupted = std::make_unique<SdGeocachingJournal>(fixture::volume);
        auto result = interrupted->begin(0, {payload, size});
        for (unsigned step = 0; step < cut && result == JournalWriteResult::InProgress; ++step)
        {
            fixture::step_io_calls = fixture::step_data_calls = 0;
            fixture::step_data_bytes = 0;
            result = interrupted->step();
            if (!stepBudgetOk()) return 90;
        }
        reached_verified = result == JournalWriteResult::Verified;
        interrupted.reset();
        const auto published = fixture::files.find("/trailmate/geocaching/.state/journal/0000000000000001.gcj");
        const bool committed = published != fixture::files.end();
        if (committed && published->second != saved.at(published->first)) return 91;
        auto retry = std::make_unique<SdGeocachingJournal>(fixture::volume);
        const auto resumed = drainJournal(*retry, retry->begin(0, {payload, size}));
        if (resumed != (committed ? JournalWriteResult::Exists : JournalWriteResult::Verified) || fixture::files != saved) return 92;
    }
    if (!reached_verified) return 93;
    fixture::files.clear();
    if (drainJournal(*journal, journal->begin(0, &mutation, 1)) != JournalWriteResult::Verified || fixture::files != saved) return 80;
    for (size_t offset : {size_t(0), size_t(24), size_t(24 + size - 1)})
    {
        fixture::files.clear();
        fixture::corrupt_at = offset;
        if (drainJournal(*journal, journal->begin(0, &mutation, 1)) != JournalWriteResult::IoError) return 81;
    }
    fixture::corrupt_at = SIZE_MAX;
    fixture::files = saved;
    const auto writes = fixture::writes;
    if (drainJournal(*journal, journal->begin(0, {payload, size})) != JournalWriteResult::Exists ||
        fixture::writes != writes || fixture::files != saved) return 3;
    fixture::files.clear();
    fixture::write_limit = 3;
    if (drainJournal(*journal, journal->begin(0, {payload, size})) != JournalWriteResult::IoError || fixture::files.empty()) return 4;
    fixture::files.clear();
    fixture::write_limit = SIZE_MAX;
    fixture::flush_ok = false;
    if (drainJournal(*journal, journal->begin(0, {payload, size})) != JournalWriteResult::IoError) return 5;
    fixture::flush_ok = true;
    for (size_t offset : {size_t(0), size_t(24)})
    {
        fixture::files.clear();
        fixture::corrupt_at = offset;
        if (drainJournal(*journal, journal->begin(0, {payload, size})) != JournalWriteResult::IoError) return 6;
    }
    fixture::files.clear();
    fixture::corrupt_at = SIZE_MAX;
    fixture::busy = true;
    if (drainJournal(*journal, journal->begin(0, {payload, size})) != JournalWriteResult::Unavailable || !fixture::files.empty()) return 7;
    fixture::busy = false;
    if (drainJournal(*journal, journal->begin(1, {payload, size})) != JournalWriteResult::Invalid || !fixture::files.empty()) return 8;
    fixture::supported_volume = false;
    if (drainJournal(*journal, journal->begin(0, {payload, size})) != JournalWriteResult::UnsupportedVolume || !fixture::files.empty()) return 9;
    fixture::supported_volume = true;
    fixture::volume[0] = 1;
    if (drainJournal(*journal, journal->begin(0, {payload, size})) != JournalWriteResult::VolumeChanged || !fixture::files.empty()) return 10;
    uint8_t state_a[1024]{}, state_b[1024]{};
    ::geocaching::storage::LogicalState live(state_a, state_b, sizeof(state_a));
    auto store = std::make_unique<SdRequestStore>(fixture::volume, 0, live);
    ::geocaching::RequestId request_id;
    uint8_t request[128]{};
    size_t request_size = 0;
    if (!::geocaching::protocol::encodeCapabilitiesRequest(request_id, request, sizeof(request), request_size)) return 11;
    if (drainStore(*store, store->persistNewTask({}, {}, request_id, {}, 3, {request, request_size}, {})) != JournalWriteResult::Verified ||
        store->committedSequence() != 1 || store->needsRecovery() || live.view().size() != 2) return 12;
    const auto writes_after_commit = fixture::writes;
    if (drainStore(*store, store->persistNewTask({}, {}, request_id, {}, 3, {request, request_size}, {})) != JournalWriteResult::StateRejected ||
        fixture::writes != writes_after_commit || store->needsRecovery()) return 17;
    const auto& committed = fixture::files.at("/trailmate/geocaching/.state/journal/0000000000000001.gcj");
    ::geocaching::storage::RecordFrameView frame;
    ::geocaching::storage::TransactionView transaction;
    ::geocaching::storage::MutationView entries[2];
    ::geocaching::storage::OutgoingView outgoing;
    ::geocaching::storage::TaskView task;
    if (!::geocaching::storage::decodeRecordFrame({reinterpret_cast<const uint8_t*>(committed.data()), committed.size()}, frame) ||
        frame.sequence != 1 || !::geocaching::storage::decodeTransaction(frame.payload, 0, entries, 2, transaction) ||
        !::geocaching::storage::decodeOutgoing(entries[0].key, entries[0].value, outgoing) ||
        !::geocaching::storage::decodeTask(entries[1].key, entries[1].value, task) ||
        !::geocaching::storage::requestBelongsToTask(entries[1].key, task, entries[0].key, outgoing)) return 13;
    fixture::flush_ok = false;
    std::array<uint8_t, 16> next_task{};
    next_task[0] = 1;
    request_id.bytes[0] = 1;
    if (!::geocaching::protocol::encodeCapabilitiesRequest(request_id, request, sizeof(request), request_size)) return 14;
    if (drainStore(*store, store->persistNewTask({}, {}, request_id, next_task, 3, {request, request_size}, {})) != JournalWriteResult::IoError ||
        store->committedSequence() != 1 || !store->needsRecovery() || live.view().size() != 2) return 15;
    fixture::flush_ok = true;
    const auto writes_after_failure = fixture::writes;
    if (drainStore(*store, store->persistNewTask({}, {}, request_id, {}, 3, {request, request_size}, {})) != JournalWriteResult::Unavailable ||
        fixture::writes != writes_after_failure || store->committedSequence() != 1) return 16;
    fixture::files.clear();
    uint8_t author_a[512]{}, author_b[512]{}, author_key[64]{};
    ::geocaching::storage::LogicalState author_state(author_a, author_b, sizeof(author_a));
    auto author_store = std::make_unique<SdRequestStore>(fixture::volume, 0, author_state);
    ::geocaching::GeocacheId cache_id;
    ::geocaching::RevisionHash hash;
    if (drainStore(*author_store, author_store->reserveAuthorVersion(cache_id, 1, hash, {author_key, 64}, {})) != JournalWriteResult::Verified ||
        author_store->committedSequence() != 1 || author_state.view().size() != 1) return 18;
    const auto reservation_writes = fixture::writes;
    if (drainStore(*author_store, author_store->reserveAuthorVersion(cache_id, 1, hash, {author_key, 64}, {})) != JournalWriteResult::Verified ||
        fixture::writes != reservation_writes || author_store->committedSequence() != 1) return 19;
    hash.bytes[0] = 1;
    if (drainStore(*author_store, author_store->reserveAuthorVersion(cache_id, 1, hash, {author_key, 64}, {})) != JournalWriteResult::StateRejected ||
        fixture::writes != reservation_writes || author_state.view().size() != 1) return 20;
    if (drainStore(*author_store, author_store->reserveAuthorVersion(cache_id, 2, hash, {author_key, 64}, {})) != JournalWriteResult::Verified ||
        author_store->committedSequence() != 2 || author_state.view().size() != 2) return 21;
    std::ifstream vector_file(argv[1], std::ios::binary);
    std::vector<uint8_t> vector_bytes((std::istreambuf_iterator<char>(vector_file)), {});
    ::geocaching::protocol::CmpReader vector_reader({vector_bytes.data(), vector_bytes.size()});
    size_t fields = 0;
    ::geocaching::ByteView encoded;
    if (!vector_reader.array(fields, 2) || !vector_reader.binary(encoded, 4096)) return 23;
    fixture::files.clear();
    uint8_t unsigned_a[512]{}, unsigned_b[512]{};
    ::geocaching::storage::LogicalState unsigned_state(unsigned_a, unsigned_b, sizeof(unsigned_a));
    auto unsigned_store = std::make_unique<SdRequestStore>(fixture::volume, 0, unsigned_state);
    NativeRecordCrypto crypto;
    std::array<uint8_t, 4166> crypto_workspace{};
    if (drainStore(*unsigned_store, unsigned_store->reserveUnsignedRecord(encoded, crypto, crypto_workspace.data(), crypto_workspace.size(), {})) != JournalWriteResult::Verified) return 24;
    ::geocaching::GeocacheId derived_id;
    ::geocaching::RevisionHash derived_hash;
    if (::geocaching::protocol::deriveGeocacheHashes(encoded, crypto, crypto_workspace.data(), crypto_workspace.size(), derived_id, derived_hash) !=
        ::geocaching::protocol::VerificationResult::Valid) return 25;
    uint8_t issued_key[36]{};
    std::memcpy(issued_key, derived_id.bytes.data(), 32);
    issued_key[35] = 1;
    ::geocaching::ByteView issued_value;
    ::geocaching::storage::AuthorIssuedView issued;
    if (!unsigned_state.view().find(3, {issued_key, sizeof(issued_key)}, issued_value) ||
        !::geocaching::storage::decodeAuthorIssued({issued_key, sizeof(issued_key)}, issued_value, issued) ||
        std::memcmp(issued.revision_hash.data, derived_hash.bytes.data(), 32)) return 26;
    ::geocaching::RecordView changed;
    if (!::geocaching::protocol::decodeGeocacheRecord(encoded, changed)) return 27;
    changed.name = "Changed";
    std::array<uint8_t, 4096> changed_bytes{};
    size_t changed_size = 0;
    if (!::geocaching::protocol::encodeGeocacheRecord(changed, changed_bytes.data(), changed_bytes.size(), changed_size)) return 28;
    const auto before_conflict = fixture::writes;
    if (drainStore(*unsigned_store, unsigned_store->reserveUnsignedRecord({changed_bytes.data(), changed_size}, crypto, crypto_workspace.data(), crypto_workspace.size(), {})) !=
            JournalWriteResult::StateRejected ||
        unsigned_store->committedSequence() != 1 || fixture::writes != before_conflict) return 29;
    ::geocaching::ByteView record_signature;
    if (!vector_reader.binary(record_signature, 64)) return 30;
    std::ifstream response_file(argv[2], std::ios::binary);
    std::vector<uint8_t> response((std::istreambuf_iterator<char>(response_file)), {});
    fixture::files.clear();
    uint8_t publish_a[2048]{}, publish_b[2048]{}, publish_request[512]{};
    ::geocaching::storage::LogicalState publish_state(publish_a, publish_b, sizeof(publish_a));
    auto publish_store = std::make_unique<SdRequestStore>(fixture::volume, 0, publish_state);
    ::geocaching::RequestId publish_id;
    publish_id.bytes.fill(2);
    size_t publish_size = 0;
    if (!::geocaching::protocol::encodePublishRequest(publish_id, encoded, record_signature, 512, publish_request, sizeof(publish_request), publish_size)) return 31;
    ::geocaching::storage::RequestTaskTarget publish_target{{derived_id.bytes.data(), 32}, {derived_hash.bytes.data(), 32}, 0};
    if (drainStore(*publish_store, publish_store->persistNewTask({}, {}, publish_id, {}, 1, {publish_request, publish_size}, {}, publish_target)) != JournalWriteResult::Verified) return 32;
    auto wrong_response = response;
    wrong_response.back() ^= 1;
    if (drainStore(*publish_store, publish_store->commitPublishResult({}, {}, publish_id, {wrong_response.data(), wrong_response.size()}, crypto)) != JournalWriteResult::Invalid ||
        publish_store->committedSequence() != 1) return 33;
    if (drainStore(*publish_store, publish_store->commitPublishResult({}, {}, publish_id, {response.data(), response.size()}, crypto)) != JournalWriteResult::Verified ||
        publish_store->committedSequence() != 2) return 34;
    uint8_t task_key[16]{};
    ::geocaching::ByteView task_bytes;
    ::geocaching::storage::TaskView completed_task;
    if (!publish_state.view().find(10, {task_key, 16}, task_bytes) || !::geocaching::storage::decodeTask({task_key, 16}, task_bytes, completed_task) ||
        completed_task.state != 3) return 35;
    const auto committed_writes = fixture::writes;
    if (drainStore(*publish_store, publish_store->commitPublishResult({}, {}, publish_id, {response.data(), response.size()}, crypto)) != JournalWriteResult::Verified ||
        publish_store->committedSequence() != 2 || fixture::writes != committed_writes) return 36;
    fixture::files.clear();
    uint8_t stopped_a[2048]{}, stopped_b[2048]{};
    ::geocaching::storage::LogicalState stopped_state(stopped_a, stopped_b, sizeof(stopped_a));
    auto stopped_store = std::make_unique<SdRequestStore>(fixture::volume, 0, stopped_state);
    if (drainStore(*stopped_store, stopped_store->persistNewTask({}, {}, publish_id, {}, 1, {publish_request, publish_size}, {}, publish_target)) != JournalWriteResult::Verified ||
        drainStore(*stopped_store, stopped_store->stopTask({})) != JournalWriteResult::Verified || stopped_store->committedSequence() != 2) return 37;
    if (!stopped_state.view().find(10, {task_key, 16}, task_bytes) || !::geocaching::storage::decodeTask({task_key, 16}, task_bytes, completed_task) ||
        completed_task.state != 5 || completed_task.continue_intent) return 38;
    const auto stop_writes = fixture::writes;
    if (drainStore(*stopped_store, stopped_store->stopTask({})) != JournalWriteResult::Verified || fixture::writes != stop_writes ||
        drainStore(*stopped_store, stopped_store->commitPublishResult({}, {}, publish_id, {response.data(), response.size()}, crypto)) != JournalWriteResult::StateRejected ||
        stopped_store->committedSequence() != 2 || fixture::writes != stop_writes) return 39;
    fixture::files.clear();
    uint8_t attempt_a[2048]{}, attempt_b[2048]{}, request_key[48]{};
    std::memcpy(request_key + 32, publish_id.bytes.data(), 16);
    ::geocaching::storage::LogicalState attempt_state(attempt_a, attempt_b, sizeof(attempt_a));
    auto attempt_store = std::make_unique<SdRequestStore>(fixture::volume, 0, attempt_state);
    if (drainStore(*attempt_store, attempt_store->persistNewTask({}, {}, publish_id, {}, 1, {publish_request, publish_size}, {}, publish_target)) != JournalWriteResult::Verified) return 40;
    std::array<uint8_t, 16> attempt_id{};
    attempt_id[0] = 1;
    if (drainStore(*attempt_store, attempt_store->beginAttempt({}, {request_key, 48}, attempt_id, {})) != JournalWriteResult::Verified ||
        attempt_store->committedSequence() != 2 || attempt_state.view().size() != 3) return 41;
    ::geocaching::storage::PendingRequestView pending;
    if (::geocaching::storage::nextPendingRequest(attempt_state.view(), {}, {}, pending) != ::geocaching::storage::PendingRequestResult::None) return 42;
    const auto attempt_writes = fixture::writes;
    if (drainStore(*attempt_store, attempt_store->beginAttempt({}, {request_key, 48}, attempt_id, {})) != JournalWriteResult::StateRejected || fixture::writes != attempt_writes) return 43;
    uint8_t attempt_key[64]{};
    std::memcpy(attempt_key, request_key, 48);
    std::memcpy(attempt_key + 48, attempt_id.data(), 16);
    ::geocaching::ByteView attempt_value;
    ::geocaching::storage::TxAttemptView attempt;
    if (!attempt_state.view().find(13, {attempt_key, 64}, attempt_value) ||
        !::geocaching::storage::decodeTxAttempt({attempt_key, 64}, attempt_value, attempt) ||
        attempt.state != ::geocaching::storage::TxAttemptState::Accepted) return 44;
    std::array<uint8_t, 32> lxmf_hash{};
    lxmf_hash[0] = 9;
    if (drainStore(*attempt_store, attempt_store->recordAttemptHash({attempt_key, 64}, lxmf_hash)) != JournalWriteResult::Verified ||
        attempt_store->committedSequence() != 3) return 45;
    if (!attempt_state.view().find(13, {attempt_key, 64}, attempt_value) ||
        !::geocaching::storage::decodeTxAttempt({attempt_key, 64}, attempt_value, attempt) ||
        attempt.state != ::geocaching::storage::TxAttemptState::InFlight || attempt.lxmf_hash.size != 32 || attempt.lxmf_hash.data[0] != 9) return 46;
    const auto hash_writes = fixture::writes;
    if (drainStore(*attempt_store, attempt_store->recordAttemptHash({attempt_key, 64}, lxmf_hash)) != JournalWriteResult::Verified || fixture::writes != hash_writes) return 47;
    lxmf_hash[0] = 10;
    if (drainStore(*attempt_store, attempt_store->recordAttemptHash({attempt_key, 64}, lxmf_hash)) != JournalWriteResult::StateRejected ||
        fixture::writes != hash_writes || attempt_store->committedSequence() != 3) return 48;
    using AttemptState = ::geocaching::storage::TxAttemptState;
    if (drainStore(*attempt_store, attempt_store->finishAttempt({attempt_key, 64}, AttemptState::Delivered, {})) != JournalWriteResult::Verified ||
        attempt_store->committedSequence() != 4) return 49;
    ::geocaching::ByteView outgoing_value;
    ::geocaching::storage::OutgoingView delivered_request;
    if (!attempt_state.view().find(5, {request_key, 48}, outgoing_value) ||
        !::geocaching::storage::decodeOutgoing({request_key, 48}, outgoing_value, delivered_request) || delivered_request.state != 2) return 50;
    const auto finish_writes = fixture::writes;
    if (drainStore(*attempt_store, attempt_store->finishAttempt({attempt_key, 64}, AttemptState::Delivered, {})) != JournalWriteResult::Verified ||
        drainStore(*attempt_store, attempt_store->finishAttempt({attempt_key, 64}, AttemptState::Failed, {})) != JournalWriteResult::StateRejected ||
        fixture::writes != finish_writes) return 51;
    fixture::files.clear();
    uint8_t failed_a[2048]{}, failed_b[2048]{};
    ::geocaching::storage::LogicalState failed_state(failed_a, failed_b, sizeof(failed_a));
    auto failed_store = std::make_unique<SdRequestStore>(fixture::volume, 0, failed_state);
    if (drainStore(*failed_store, failed_store->persistNewTask({}, {}, publish_id, {}, 1, {publish_request, publish_size}, {}, publish_target)) != JournalWriteResult::Verified ||
        drainStore(*failed_store, failed_store->beginAttempt({}, {request_key, 48}, attempt_id, {})) != JournalWriteResult::Verified ||
        drainStore(*failed_store, failed_store->finishAttempt({attempt_key, 64}, AttemptState::Failed, {})) != JournalWriteResult::Verified) return 52;
    if (::geocaching::storage::nextPendingRequest(failed_state.view(), {}, {}, pending) != ::geocaching::storage::PendingRequestResult::Ready) return 53;
    if (drainStore(*failed_store, failed_store->stopTask({})) != JournalWriteResult::Verified ||
        ::geocaching::storage::nextPendingRequest(failed_state.view(), {}, {}, pending) != ::geocaching::storage::PendingRequestResult::None) return 54;
    fixture::files.clear();
    uint8_t timeout_a[2048]{}, timeout_b[2048]{};
    ::geocaching::storage::LogicalState timeout_state(timeout_a, timeout_b, sizeof(timeout_a));
    auto timeout_store = std::make_unique<SdRequestStore>(fixture::volume, 0, timeout_state);
    ::geocaching::storage::StoredTime submitted, later;
    submitted.monotonic_ms = 100;
    later.monotonic_ms = 1099;
    if (drainStore(*timeout_store, timeout_store->persistNewTask({}, {}, publish_id, {}, 1, {publish_request, publish_size}, {}, publish_target)) != JournalWriteResult::Verified ||
        drainStore(*timeout_store, timeout_store->beginAttempt({}, {request_key, 48}, attempt_id, submitted)) != JournalWriteResult::Verified) return 55;
    bool expired = false;
    if (drainStore(*timeout_store, timeout_store->expireOneAttempt(later, 0, 1000, expired)) != JournalWriteResult::Verified || expired || timeout_store->committedSequence() != 2) return 56;
    later.monotonic_ms = 1100;
    if (drainStore(*timeout_store, timeout_store->expireOneAttempt(later, 0, 1000, expired)) != JournalWriteResult::Verified || expired || timeout_store->committedSequence() != 3 ||
        ::geocaching::storage::nextPendingRequest(timeout_state.view(), {}, {}, pending) != ::geocaching::storage::PendingRequestResult::Ready) return 57;
    if (drainStore(*timeout_store, timeout_store->expireOneAttempt(later, 0, 1000, expired)) != JournalWriteResult::Verified || expired || timeout_store->committedSequence() != 3) return 58;
    fixture::files.clear();
    uint8_t caps_a[1024]{}, caps_b[1024]{}, caps_request[128]{};
    ::geocaching::storage::LogicalState caps_state(caps_a, caps_b, sizeof(caps_a));
    auto caps_store = std::make_unique<SdRequestStore>(fixture::volume, 0, caps_state);
    ::geocaching::RequestId caps_id;
    caps_id.bytes.fill(1);
    size_t caps_size = 0;
    if (!::geocaching::protocol::encodeCapabilitiesRequest(caps_id, caps_request, sizeof(caps_request), caps_size) ||
        drainStore(*caps_store, caps_store->persistNewTask({}, {}, caps_id, {}, 3, {caps_request, caps_size}, {})) != JournalWriteResult::Verified) return 59;
    std::ifstream caps_file(argv[3], std::ios::binary);
    std::vector<uint8_t> caps_response((std::istreambuf_iterator<char>(caps_file)), {});
    ::geocaching::Destination wrong_remote;
    wrong_remote.bytes[0] = 9;
    if (drainStore(*caps_store, caps_store->commitDirectoryCapabilities({}, wrong_remote, caps_id, {caps_response.data(), caps_response.size()})) != JournalWriteResult::StateRejected) return 60;
    if (drainStore(*caps_store, caps_store->commitDirectoryCapabilities({}, {}, caps_id, {caps_response.data(), caps_response.size()})) != JournalWriteResult::Verified ||
        caps_store->committedSequence() != 2) return 61;
    const auto caps_writes = fixture::writes;
    if (drainStore(*caps_store, caps_store->commitDirectoryCapabilities({}, {}, caps_id, {caps_response.data(), caps_response.size()})) != JournalWriteResult::Verified ||
        fixture::writes != caps_writes) return 62;
    return 0;
}
