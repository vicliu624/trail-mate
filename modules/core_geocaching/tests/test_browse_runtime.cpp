#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/query_response.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "runtime_environment.h"
#include "storage_owner.h"
#include "ui_presentation/geocaching/local_map_overlay.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace rt = platform::esp::arduino_common::geocaching::browse_runtime;
namespace test = runtime_test;
using Section = ui::geocaching::Section;
uint32_t tick_ms = 5;
std::vector<uint8_t> last_reply;

void require(bool result, const char* message)
{
    if (!result)
    {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
void tick()
{
    test::clock_ms += tick_ms;
    test::io_bytes = 0;
    test::in_ui = false;
    test::maintenance::tick(static_cast<uint32_t>(test::clock_ms));
    test::in_ui = true;
    require(test::io_bytes <= 512, "runtime exceeded the per-slice transfer budget");
}
template <class Predicate>
void until(Predicate ready, const char* message, bool force_profile = false)
{
    test::profile_reads = force_profile || std::getenv("TRAIL_MATE_TEST_IO_PROFILE") != nullptr;
    test::read_bytes_by_path.clear();
    const auto started = test::clock_ms;
    const auto begins = test::maintenance::begins;
    const auto operations = test::io_operations;
    unsigned ticks = 0;
    for (; ticks < 100000 && !ready(); ++ticks) tick();
    std::fprintf(stderr, "Runtime wait: %s; ticks=%u simulated_ms=%llu owner_begins=%llu io_operations=%llu\n", message, ticks,
                 static_cast<unsigned long long>(test::clock_ms - started),
                 static_cast<unsigned long long>(test::maintenance::begins - begins),
                 static_cast<unsigned long long>(test::io_operations - operations));
    if (test::profile_reads)
    {
        std::vector<std::pair<uint64_t, std::string>> reads;
        uint64_t total = 0;
        for (const auto& file : test::read_bytes_by_path)
        {
            reads.emplace_back(file.second, file.first);
            total += file.second;
        }
        std::sort(reads.rbegin(), reads.rend());
        std::fprintf(stderr, "Runtime read profile: total=%llu bytes distinct_files=%u\n", static_cast<unsigned long long>(total), static_cast<unsigned>(reads.size()));
        for (size_t i = 0; i < (force_profile ? reads.size() : std::min<size_t>(5, reads.size())); ++i)
            std::fprintf(stderr, "  %llu bytes %s\n", static_cast<unsigned long long>(reads[i].first), reads[i].second.c_str());
    }
    if (!ready())
    {
        ui::geocaching::Snapshot view;
        test::source->snapshot(Section::Discover, view);
        std::fprintf(stderr, "Runtime terminal status: %s\n", view.status.data());
        test::source->snapshot(Section::Published, view);
        std::fprintf(stderr, "Runtime storage status: %s\n", view.status.data());
        for (const auto& file : test::files)
            if (file.first.find("checkpoint/") != std::string::npos || file.first.find("staging/") != std::string::npos)
                std::fprintf(stderr, "Maintenance file: %s (%u bytes)\n", file.first.c_str(), static_cast<unsigned>(file.second.size()));
    }
    require(ready(), message);
}
ui::geocaching::Snapshot snapshot(Section section)
{
    test::in_ui = true;
    ui::geocaching::Snapshot value;
    test::source->snapshot(section, value);
    require(!test::ui_io, "UI callback touched storage");
    return value;
}
std::vector<uint8_t> fixture(const std::string& folder, const char* name)
{
    std::ifstream file(folder + '/' + name, std::ios::binary);
    require(file.good(), "fixture missing");
    return {(std::istreambuf_iterator<char>(file)), {}};
}
void announce(chat::MeshAdapterRouter& router)
{
    std::array<uint8_t, 16> destination{};
    std::array<uint8_t, 64> key{};
    std::vector<uint8_t> app{0x95, 1, 0xc4, 16};
    app.insert(app.end(), 16, 0);
    app.insert(app.end(), {0xc4, 16});
    app.insert(app.end(), 16, 0);
    app.insert(app.end(), {0, 0xa1, 'x'});
    router.announcement({{destination.data(), destination.size()}, {destination.data(), destination.size()}, {key.data(), key.size()}, {app.data(), app.size()}}, router.context);
}
void reply(chat::MeshAdapterRouter& router, std::vector<uint8_t> response, unsigned expected_operation)
{
    geocaching::protocol::CmpReader request({router.sent_bytes.data(), router.sent_bytes.size()});
    geocaching::protocol::CmpReader incoming({response.data(), response.size()});
    size_t count = 0;
    uint64_t version = 0, kind = 0, operation = 0;
    geocaching::ByteView id, response_id;
    require(request.array(count, 16) && request.unsignedInteger(version) && request.unsignedInteger(kind) &&
                request.unsignedInteger(operation) && operation == expected_operation && request.binary(id, 16),
            "unexpected outgoing request");
    require(incoming.array(count, 16) && incoming.unsignedInteger(version) && incoming.unsignedInteger(kind) &&
                incoming.unsignedInteger(operation) && operation == expected_operation && incoming.binary(response_id, 16),
            "invalid reply fixture");
    std::memcpy(response.data() + (response_id.data - response.data()), id.data, 16);
    std::array<uint8_t, 16> remote{};
    require(router.delivery != nullptr, "delivery callback missing");
    const auto semaphore = xSemaphoreCreateMutex();
    require(xSemaphoreTake(semaphore, 0) == pdTRUE, "cannot occupy the storage mutex during delivery");
    require(!router.delivery({{remote.data(), remote.size()}, {router.local.data(), router.local.size()}, {}, {response.data(), response.size()}}, router.context),
            "volatile reply acknowledged before persistence");
    // A duplicate while full must not overwrite the first owned payload or
    // acknowledge it early. Destroy borrowed network bytes before SD resumes.
    require(!router.delivery({{remote.data(), remote.size()}, {router.local.data(), router.local.size()}, {}, {response.data(), response.size()}}, router.context),
            "full receive mailbox acknowledged a volatile response");
    last_reply = response;
    std::fill(response.begin(), response.end(), 0xa5);
    xSemaphoreGive(semaphore);
}
std::vector<uint8_t> paginationFixture(const std::string& folder, bool final_page)
{
    auto response = fixture(folder, "query-response-v1.bin");
    geocaching::protocol::CmpReader envelope({response.data(), response.size()});
    size_t count = 0;
    uint64_t value = 0;
    geocaching::ByteView id;
    require(envelope.array(count, 6) && envelope.unsignedInteger(value) && envelope.unsignedInteger(value) &&
                envelope.unsignedInteger(value) && envelope.binary(id, 16),
            "pagination fixture envelope");
    geocaching::RequestId request;
    std::memcpy(request.bytes.data(), id.data, 16);
    geocaching::protocol::QueryPageView page;
    require(geocaching::protocol::decodeQueryPage({response.data(), response.size()}, request, 2048, 20, page), "pagination fixture page");
    if (!final_page)
    {
        const auto offset = page.encoded_items.data + page.encoded_items.size - response.data();
        require(response[offset] == 0xc0, "pagination fixture already has cursor");
        response.erase(response.begin() + offset);
        response.insert(response.begin() + offset, {0xc4, 1, 0x7c});
        return response;
    }
    std::vector<uint8_t> empty(128);
    geocaching::protocol::CmpWriter writer(empty.data(), empty.size());
    require(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(2) &&
                writer.binary({request.bytes.data(), 16}) && writer.unsignedInteger(200) && writer.array(4) &&
                writer.binary(page.snapshot_id) && writer.array(0) && writer.nil() && writer.unsignedInteger(page.remaining_ttl),
            "encode final page");
    empty.resize(writer.size());
    return empty;
}
std::vector<uint8_t> publicationReply(chat::MeshAdapterRouter& router, uint32_t revision)
{
    geocaching::protocol::CmpReader envelope({router.sent_bytes.data(), router.sent_bytes.size()});
    size_t count = 0;
    uint64_t value = 0;
    geocaching::ByteView bytes;
    require(envelope.array(count, 6) && envelope.unsignedInteger(value) && envelope.unsignedInteger(value) &&
                envelope.unsignedInteger(value) && value == 1 && envelope.binary(bytes, 16),
            "publication envelope missing");
    geocaching::RequestId request;
    std::memcpy(request.bytes.data(), bytes.data, 16);
    geocaching::protocol::PublishRequestView publication;
    geocaching::protocol::VerifiedRecordView record;
    std::array<uint8_t, 4166> scratch;
    platform::esp::common::EspGeocachingCrypto crypto;
    require(geocaching::protocol::decodePublishRequest({router.sent_bytes.data(), router.sent_bytes.size()}, request, publication) &&
                geocaching::protocol::verifyGeocache(publication.signed_cache, crypto, scratch.data(), scratch.size(), record) == geocaching::protocol::VerificationResult::Valid &&
                record.record.revision == revision,
            "published record has invalid signature or revision");
    std::vector<uint8_t> response(512);
    geocaching::protocol::CmpWriter writer(response.data(), response.size());
    require(writer.array(6) && writer.unsignedInteger(1) && writer.unsignedInteger(1) && writer.unsignedInteger(1) &&
                writer.binary({request.bytes.data(), 16}) && writer.unsignedInteger(200) && writer.array(7) &&
                writer.binary({record.id.bytes.data(), 32}) && writer.unsignedInteger(revision) && writer.binary({record.hash.bytes.data(), 32}) &&
                writer.unsignedInteger(0) && writer.unsignedInteger(revision) && writer.binary({record.hash.bytes.data(), 32}) &&
                writer.unsignedInteger(static_cast<uint8_t>(record.record.state)),
            "could not encode publication receipt");
    response.resize(writer.size());
    return response;
}
void appendMaintenanceHistory(unsigned count)
{
    namespace sd = platform::esp::arduino_common::geocaching;
    namespace gc = geocaching::storage;
    test::in_ui = false;
    gc::VolumeInstance volume;
    require(sd::inspectSdVolume(volume) == sd::SdVolumeResult::Ready, "history volume missing");
    std::array<gc::IndexRootBytes, 2> roots;
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto& bytes = test::files.at("/trailmate/geocaching/.state/index/root.h" + std::to_string(i));
        require(bytes.size() == roots[i].size(), "history root size");
        std::copy(bytes.begin(), bytes.end(), roots[i].begin());
    }
    gc::IndexRootView first, second, root;
    require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, first) &&
                gc::decodeIndexRoot({roots[1].data(), roots[1].size()}, volume, second) && gc::selectIndexRoot(first, second, root),
            "history root selection");
    unsigned copy = second.revision > first.revision ? 1 : 0;
    std::array<uint8_t, 8192> frame;
    std::array<uint8_t, 32> key;
    key.fill(0xcc);
    const uint8_t value[] = {0x92, 0x90, 0x90};
    const gc::MutationView row{11, {key.data(), key.size()}, {value, sizeof(value)}, false};
    for (unsigned transaction = 0; transaction < count; ++transaction)
    {
        sd::SdIndexedCommit commit(volume);
        require(commit.begin(root, copy, &row, 1, frame.data(), frame.size(), roots[1 - copy]), "history commit begin");
        auto status = sd::IndexedCommitStep::Working;
        for (unsigned i = 0; i < 100000 && status == sd::IndexedCommitStep::Working; ++i) status = commit.step();
        require(status == sd::IndexedCommitStep::Verified && commit.committed(root), "history commit failed");
        copy = 1 - copy;
    }
}
struct DetailText
{
    ui::geocaching::DetailStatus status = ui::geocaching::DetailStatus::Pending;
    std::string description, hint, error;
    bool can_archive = false;
};
DetailText detailText(const ui::geocaching::Item& item)
{
    test::in_ui = true;
    DetailText result;
    test::source->readDetail(
        item.id, item.revision_hash, [](const ui::geocaching::DetailView& value, void* context)
        {
        auto& out = *static_cast<DetailText*>(context);
        out.status = value.status;
        out.description = value.description;
        out.hint = value.hint;
        out.can_archive = value.can_archive;
        out.error = value.error; },
        &result);
    require(!test::ui_io, "detail UI callback touched SD");
    return result;
}
int main(int argc, char** argv)
{
    require(argc == 2 || argc == 3, "expected fixture directory and optional runtime scenario");
    const bool close_on_save = argc == 3 && std::strcmp(argv[2], "save-close-detail") == 0;
    const std::string folder(argv[1]);
    const auto encoded_record = fixture(folder, "record-v1.bin");
    geocaching::RecordView expected_record;
    require(geocaching::protocol::decodeGeocacheRecord({encoded_record.data(), encoded_record.size()}, expected_record), "detail record fixture invalid");
    std::fprintf(stderr, "Runtime: discovery/download/publication\n");
    chat::MeshAdapterRouter router;
    LoraBoard board;
    rt::configure(router, board);
    if (argc == 3 && !close_on_save)
    {
        require(std::strcmp(argv[2], "local-draft-only") == 0, "unknown runtime scenario");
        router.ready = false;
        test::in_ui = true;
        test::source->activate(true);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "empty offline card did not permit a local draft");
        ui::geocaching::DraftInput local;
        local.name = "Unpublished local draft";
        local.description = "Saved without any download or directory connection";
        local.has_coordinates = true;
        local.latitude_e7 = 310000000;
        local.longitude_e7 = 1210000000;
        require(test::source->saveDraft(local), "offline draft was not queued");
        until([&]
              { return test::source->draftSaveStatus(local.id, 0) == ui::geocaching::DraftSaveStatus::Saved; },
              "offline draft was not saved");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "draft editor session did not close");
        const auto saved_files = test::files;
        // Reproduce the L2 heap snapshot: PSRAM is available, internal heap is
        // below the unrelated 40 KiB reserve used by network admissions.
        test::internal_free = 38904;
        test::io_delay_ms = std::getenv("TRAIL_MATE_TEST_IO_DELAY_MS")
                                ? static_cast<uint32_t>(std::strtoul(std::getenv("TRAIL_MATE_TEST_IO_DELAY_MS"), nullptr, 10))
                                : 5;
        test::source->activate(true);
        until([&]
              { return std::strstr(snapshot(Section::Downloaded).status.data(), "Restoring geocaching tasks"); },
              "map reopen did not start storage");
        // A map tile read can contend with the newly opened local catalogue.
        // One failed read must not permanently disable the map's local layer.
        test::fail_read = true;
        auto overlays = std::make_unique<ui::geocaching::LocalMapOverlay>();
        auto map = std::make_unique<ui::map::MapOverlaySnapshot>();
        const auto started = test::clock_ms;
        for (unsigned frame = 0; frame < 12 && !map->item_count; ++frame)
        {
            const auto due = test::clock_ms + 750;
            overlays->update(*test::source, 31, 121, 15);
            map = std::make_unique<ui::map::MapOverlaySnapshot>();
            overlays->append(*map);
            if (map->item_count) break;
            while (test::clock_ms < due) tick();
        }
        require(map->item_count == 1 && map->header.valid, "main Map lost the only unpublished local draft");
        require(map->items[0].point.valid && map->items[0].point.lat == 31 && map->items[0].point.lon == 121,
                "local draft coordinates did not reach Map");
        require(map->items[0].style == ui::map::MapOverlayStyle::Warning, "local draft marker state lost");
        std::fprintf(stderr, "Draft-only Map elapsed_ms=%llu\n", static_cast<unsigned long long>(test::clock_ms - started));
        require(test::clock_ms - started <= 3000, "draft-only Map exceeded three seconds");
        require(test::files == saved_files && !router.sends && !router.service, "local Map wrote storage or started networking");
        bool draft_loaded = false;
        const auto read_started = test::clock_ms;
        until([&]
              { return draft_loaded || test::source->readDraft(
                                           local.id, [](const ui::geocaching::DraftInput& value, void* context)
                                           {
                          require(value.name == "Unpublished local draft" && value.has_coordinates, "low-heap draft lost its content");
                          *static_cast<bool*>(context) = true; },
                                           &draft_loaded) == ui::geocaching::DraftReadStatus::Ready; },
              "low internal heap blocked the draft editor");
        require(draft_loaded && test::clock_ms - read_started <= 3000, "draft editor exceeded three seconds with PSRAM available");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "draft-only Map did not release storage");
        return 0;
    }
    // All interface and transport callbacks below must stay free of SD I/O.
    // Only tick() enters the storage maintenance owner.
    test::in_ui = true;
    require(test::source != nullptr, "UI source not bound");
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "new volume did not reach discovery");
    require(router.selected_chat == chat::MeshProtocol::Meshtastic, "Geocaching changed chat protocol");
    const auto semaphore = xSemaphoreCreateMutex();
    require(xSemaphoreTake(semaphore, 0) == pdTRUE, "cannot simulate a busy storage owner");
    const auto busy_view = snapshot(Section::Discover);
    // The network task can receive the only directory announcement while the
    // storage owner holds the session mutex. It must survive that contention.
    announce(router);
    xSemaphoreGive(semaphore);
    require(busy_view.busy, "storage contention was reported as an empty current snapshot");
    require(!snapshot(Section::Discover).busy, "snapshot did not recover after storage lock release");
    until([&]
          { return router.sends == 1; },
          "capabilities request not dispatched");
    // Browsing must remain disposable, even without an SD card or while USB
    // owns it. Closing abandons the request without any journal transaction.
    require(test::files.empty(), "browse startup wrote storage");
    const auto obsolete_request = router.sent_bytes;
    reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
    test::source->activate(false);
    test::external_owner = true;
    until([&]
          { return !router.service; },
          "RAM query did not close while USB owned SD");
    require(!test::allocated("geocaching.rx") && !test::blocked_io, "close touched SD or leaked reply");
    test::external_owner = false;
    test::card_ready = false;
    router.sends = 0;
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "SD-free browse did not start");
    announce(router);
    until([&]
          { return router.sends == 1; },
          "SD-free capabilities missing");
    require(router.sent_bytes != obsolete_request, "restart reused the abandoned query");
    // Real SD operations in the remote capture take tens of milliseconds.
    // Exercise the receive handoff and response scheduling at that cadence.
    tick_ms = 40;
    router.busy_sends_remaining = 3;
    reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
    until([&]
          { return router.sends == 2; },
          "query request not dispatched");
    require(router.busy_sends_remaining == 0, "query did not exercise router contention");

    reply(router, fixture(folder, "query-response-v1.bin"), 2);
    until([&]
          { const auto view = snapshot(Section::Discover); return view.count == 1 && view.can_refresh; },
          "query page not displayed");
    require(test::files.empty() && !test::blocked_io && !test::allocated("geocaching.index.read") &&
                !test::allocated("geocaching.index.encode"),
            "browsing depends on SD or its workspaces");
    test::card_ready = true;
    tick_ms = 5;
    std::array<uint8_t, 16> reply_remote{};
    require(router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {last_reply.data(), last_reply.size()}}, router.context),
            "accepted RAM reply was not acknowledged on retry");
    auto changed_reply = last_reply;
    changed_reply.back() ^= 1;
    require(!router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {changed_reply.data(), changed_reply.size()}}, router.context),
            "modified duplicate reply was acknowledged");
    reply_remote[0] = 1;
    require(!router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {last_reply.data(), last_reply.size()}}, router.context),
            "reply from another directory was acknowledged");
    reply_remote[0] = 0;
    // Protocol switching can remove a background backend between UI ticks.
    // Recreate it without losing the RAM page or opening storage.
    router.service.reset();
    until([&]
          { return router.service != nullptr; },
          "browse transport did not reconnect");
    require(snapshot(Section::Discover).count == 1 && test::files.empty(), "transport reconnection lost the RAM page");
    test::profile_reads = true;
    test::read_bytes_by_path.clear();
    for (unsigned i = 0; i < 500; ++i) tick();
    require(test::read_bytes_by_path.empty(), "completed browsing kept scanning recovery or dispatch history");
    test::profile_reads = false;
    ui::geocaching::Item item;
    uint64_t generation = 0;
    test::source->requestWindow(Section::Discover, 0, 1);
    until([&]
          {
              const auto view = snapshot(Section::Discover);
              generation = view.generation;
              return test::source->item(Section::Discover, 0, generation, item) && item.can_download; },
          "discovered row did not become downloadable");
    test::card_ready = false;
    test::source->open(item, generation);
    until([&]
          { return router.sends == 3; },
          "online detail request missing");
    auto invalid_detail = fixture(folder, "get-response-v1.bin");
    invalid_detail[invalid_detail.size() - 4] ^= 1;
    reply(router, invalid_detail, 3);
    for (unsigned i = 0; i < 30; ++i) tick();
    require(detailText(item).status == ui::geocaching::DetailStatus::Pending, "invalid detail signature was displayed");
    reply(router, fixture(folder, "get-response-v1.bin"), 3);
    until([&]
          { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
          "online detail not displayed");
    require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint,
            "online detail omitted full description or hint");
    require(test::files.empty() && !test::blocked_io, "online detail required SD");
    require(router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {last_reply.data(), last_reply.size()}}, router.context),
            "verified detail duplicate was not acknowledged");
    require(xSemaphoreTake(semaphore, 0) == pdTRUE, "cannot lock during detail close");
    test::source->closeDetail();
    xSemaphoreGive(semaphore);
    tick();
    require(detailText(item).status == ui::geocaching::DetailStatus::Failed, "busy close lost the cancellation");
    require(!test::allocated("geocaching.detail.rx") && !test::allocated("geocaching.detail.verify"), "closed detail retained temporary buffers");
    test::card_ready = true;
    test::source->open(item, generation);
    until([&]
          { return router.sends == 4; },
          "reopened detail request not dispatched");
    reply(router, fixture(folder, "get-response-v1.bin"), 3);
    until([&]
          { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
          "reopened detail not verified");
    test::source->requestWindow(Section::Published, 0, 4);
    until([&]
          { return std::strstr(snapshot(Section::Published).status.data(), "No local drafts"); },
          "initial publication projection did not load");
    const auto publication_rows_before_download = snapshot(Section::Published).generation;
    test::source->requestWindow(Section::Discover, 0, 1);
    generation = snapshot(Section::Discover).generation;
    void* open_response = nullptr;
    for (const auto& allocation : test::allocations)
        if (allocation.second.owner == "geocaching.rx") open_response = allocation.first;
    require(open_response != nullptr, "open detail response missing before save");
    test::memory_available = false;
    require(!test::source->download(item, generation), "save ignored unavailable PSRAM");
    test::memory_available = true;
    require(detailText(item).status == ui::geocaching::DetailStatus::Ready,
            "failed save invalidated the open verified detail");
    require(test::source->download(item, generation), "download not queued");
    require(test::allocated("geocaching.download.detail"), "verified download did not own a response copy");
    if (close_on_save)
    {
        // Release the original response before the installation's first tick.
        // Every verified view must belong to the queued download's copy.
        test::source->closeDetail();
        until([&]
              {
                  const auto allocation = test::allocations.find(open_response);
                  return allocation == test::allocations.end() || allocation->second.owner != "geocaching.rx"; },
              "closing pending download retained the detail response");
    }
    until([&]
          {
              if (!close_on_save)
                  require(detailText(item).status == ui::geocaching::DetailStatus::Ready,
                          "saving invalidated the open verified detail");
              return std::strstr(snapshot(Section::Discover).status.data(), "Shared caches"); },
          "download did not finish");
    if (!close_on_save)
    {
        require(test::allocations.count(open_response), "saving released the open detail response");
        require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint,
                "saved open detail lost verified text");
        require(!test::source->archiveCache(item.id, item.revision_hash), "saved foreign detail became archivable");
        test::source->closeDetail();
        tick();
    }
    require(router.sends == 4, "saving verified detail sent a redundant network request");
    const auto released_response = test::allocations.find(open_response);
    require(released_response == test::allocations.end() || released_response->second.owner != "geocaching.rx",
            "saved detail retained its response buffer");
    // A complete download writes requests, attempts, objects and installed
    // heads. None changes the publication projection we already loaded.
    require(std::strstr(snapshot(Section::Published).status.data(), "No local drafts"),
            "unrelated download invalidated the loaded publication projection");
    require(snapshot(Section::Published).generation == publication_rows_before_download,
            "unrelated download changed the publication row token");
    test::source->requestWindow(Section::Downloaded, 0, 4);
    until([&]
          {
              const auto view = snapshot(Section::Downloaded);
              return view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item) && item.downloaded; },
          "installed GPX missing from directory");
    const auto saved_id = item.id;
    if (close_on_save)
    {
        test::source->open(item, snapshot(Section::Downloaded).generation);
        until([&]
              { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
              "closed detail download lost installed details");
        require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint,
                "closed detail download corrupted installed text");
        require(router.sends == 4, "installed detail required another network request");
        test::source->closeDetail();
        test::source->activate(false);
        until([&]
              { return test::allocations.empty(); },
              "closed detail installation leaked PSRAM");
        return 0;
    }
    ui::geocaching::DraftInput draft;
    draft.name = "Published through device runtime";
    draft.description = "A local draft with a durable signed revision";
    draft.has_coordinates = true;
    draft.latitude_e7 = 310000000;
    draft.longitude_e7 = 1210000000;
    require(test::source->saveDraft(draft), "draft was not queued");
    until([&]
          { return test::source->draftSaveStatus(draft.id, 0) == ui::geocaching::DraftSaveStatus::Saved; },
          "draft was not saved");
    test::source->requestWindow(Section::Published, 0, 4);
    uint64_t draft_generation = 0;
    const auto draft_list_started = test::clock_ms;
    until([&]
          {
              const auto view = snapshot(Section::Published);
              if (!view.count || !test::source->item(Section::Published, 0, view.generation, item)) return false;
              draft_generation = item.edit_generation;
              return item.is_draft; },
          "saved draft missing from publication list");
    require((test::clock_ms - draft_list_started) / tick_ms < 40,
            "unbound draft listing regressed into publication-history work");
    {
        auto overlays = std::make_unique<ui::geocaching::LocalMapOverlay>();
        auto map = std::make_unique<ui::map::MapOverlaySnapshot>();
        until([&]
              {
                  overlays->update(*test::source, 31, 121, 15);
                  map = std::make_unique<ui::map::MapOverlaySnapshot>();
                  overlays->append(*map);
                  return map->item_count == 2; },
              "map did not include unpublished draft and downloaded cache");
        require(map->items[0].point.lat == 31 && map->items[0].point.lon == 121 &&
                    map->items[0].style == ui::map::MapOverlayStyle::Warning,
                "unpublished map position or state lost");
        test::source->requestWindow(Section::Published, 0, 4);
    }
    std::array<uint8_t, 64> author;
    uint32_t from = 0, to = 0;
    until([&]
          { return test::source->publicationAuthor(draft.id, draft_generation, author, &from, &to); },
          "draft not ready for publication confirmation");
    const bool dispatch_profile = test::profile_reads;
    std::vector<std::string> prior_attempt_shards;
    for (const auto& file : test::files)
        if (file.first.find("/0d/") != std::string::npos && file.first.size() >= 4 && file.first.substr(file.first.size() - 4) == ".gci")
            prior_attempt_shards.push_back(file.first);
    test::profile_reads = true;
    test::read_bytes_by_path.clear();
    require(from == 0 && to == 1 && test::source->publishDraft(draft.id, draft_generation, author, to), "publication confirmation rejected");
    until([&]
          { return router.sends == 5; },
          "publication request not dispatched", true);
    require(!test::read_bytes_by_path.empty(), "foreground dispatch read profile was not captured");
    for (const auto& read : test::read_bytes_by_path)
        require(std::find(prior_attempt_shards.begin(), prior_attempt_shards.end(), read.first) == prior_attempt_shards.end() || !read.second,
                "foreground publication scanned historical attempts before sending");
    test::profile_reads = dispatch_profile;
    reply(router, publicationReply(router, 1), 1);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return view.count == 1 && test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 1; },
          "publication receipt not reflected in draft list");
    require(router.identity.signs == 1, "publication signed more than once");
    // Exercise the UI field limits and the confirmed-version edit path through
    // the same device owner, not a direct store call.
    const std::string long_name(96, 'N'), long_description(2048, 'D'), long_hint(512, 'H');
    draft.generation = item.edit_generation;
    draft.name = long_name;
    draft.description = long_description;
    draft.hint = long_hint;
    require(test::source->saveDraft(draft), "confirmed draft edit was not queued");
    until([&]
          { return test::source->draftSaveStatus(draft.id, draft.generation) == ui::geocaching::DraftSaveStatus::Saved; },
          "maximum-size draft edit was not saved");
    until([&]
          {
              const auto view = snapshot(Section::Published);
              if (!test::source->item(Section::Published, 0, view.generation, item)) return false;
              draft_generation = item.edit_generation;
              return test::source->publicationAuthor(draft.id, draft_generation, author, &from, &to); },
          "edited draft not ready for publication");
    require(from == 1 && to == 2 && test::source->publishDraft(draft.id, draft_generation, author, to), "second publication confirmation rejected");
    until([&]
          { return router.sends == 6; },
          "second publication request not dispatched");
    reply(router, publicationReply(router, 2), 1);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 2; },
          "second publication receipt not reflected in list");
    require(router.identity.signs == 2, "version update signed more than once");
    // Isolate archive conformance from the following restart fixture, which
    // intentionally retains an active revision-2 draft for its map assertions.
    const auto active_fixture = test::files;
    const auto active_directories = test::directories;
    geocaching::protocol::CmpReader sent({router.sent_bytes.data(), router.sent_bytes.size()});
    size_t sent_count = 0;
    uint64_t sent_value = 0;
    geocaching::ByteView sent_id;
    require(sent.array(sent_count, 6) && sent.unsignedInteger(sent_value) && sent.unsignedInteger(sent_value) &&
                sent.unsignedInteger(sent_value) && sent.binary(sent_id, 16),
            "archive source request invalid");
    geocaching::RequestId prior_request;
    std::memcpy(prior_request.bytes.data(), sent_id.data, 16);
    geocaching::protocol::PublishRequestView published;
    geocaching::protocol::VerifiedRecordView public_record;
    std::vector<uint8_t> detail_scratch(8192), own_detail_response(8192);
    platform::esp::common::EspGeocachingCrypto public_crypto;
    require(geocaching::protocol::decodePublishRequest({router.sent_bytes.data(), router.sent_bytes.size()}, prior_request, published) &&
                geocaching::protocol::verifyGeocache(published.signed_cache, public_crypto, detail_scratch.data(), detail_scratch.size(), public_record) == geocaching::protocol::VerificationResult::Valid,
            "archive base not verified");
    ui::geocaching::Item public_item;
    public_item.id = public_record.id.bytes;
    public_item.revision_hash = public_record.hash.bytes;
    geocaching::protocol::CmpReader signed_reader(published.signed_cache);
    geocaching::ByteView archive_record_bytes, signature;
    require(signed_reader.array(sent_count, 2) && signed_reader.binary(archive_record_bytes, 4096) && signed_reader.binary(signature, 64), "archive signed pair invalid");
    geocaching::protocol::CmpWriter detail_writer(own_detail_response.data(), own_detail_response.size());
    require(detail_writer.array(6) && detail_writer.unsignedInteger(1) && detail_writer.unsignedInteger(1) && detail_writer.unsignedInteger(3) &&
                detail_writer.binary({prior_request.bytes.data(), 16}) && detail_writer.unsignedInteger(200) && detail_writer.array(3) &&
                detail_writer.array(2) && detail_writer.binary(archive_record_bytes) && detail_writer.binary(signature) && detail_writer.unsignedInteger(1) && detail_writer.unsignedInteger(0),
            "archive detail response failed");
    own_detail_response.resize(detail_writer.size());
    test::source->open(public_item, 0);
    until([&]
          { return router.sends == 7; },
          "archive detail not requested");
    reply(router, own_detail_response, 3);
    until([&]
          { return detailText(public_item).status == ui::geocaching::DetailStatus::Ready; },
          "archive detail not verified");
    require(detailText(public_item).can_archive, "owner cannot manage verified public detail");
    auto stale_archive = public_item.revision_hash;
    stale_archive[0] ^= 1;
    require(!test::source->archiveCache(public_item.id, stale_archive), "archive accepted a stale detail");
    // Transport admission may take longer than the response budget. A queued
    // request must still receive a full budget after its eventual submission.
    router.busy_sends_remaining = 8;
    require(test::source->archiveCache(public_item.id, public_item.revision_hash), "public detail archive not queued");
    until([&]
          { return router.sends == 8; },
          "archive request not dispatched");
    require(test::source->archiveStatus(public_item.id) == ui::geocaching::DraftSaveStatus::Pending, "archive confirmed without directory reply");
    reply(router, publicationReply(router, 3), 1);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return test::source->item(Section::Published, 0, view.generation, item) && item.state == 2 &&
                     item.publication_confirmed && item.publication_revision == 3; },
          "archive directory confirmation not reflected in local record");
    require(test::source->archiveStatus(public_item.id) == ui::geocaching::DraftSaveStatus::Saved, "archive detail did not show confirmation");
    // Let wall time advance: a retry must reuse the reserved timestamp rather
    // than construct different content for the same successor version.
#ifdef _WIN32
    Sleep(1100);
#else
    usleep(1100000);
#endif
    require(test::source->archiveCache(public_item.id, public_item.revision_hash), "archive retry not queued");
    const auto retry_sends = router.sends;
    until([&]
          { return router.sends > retry_sends || test::source->archiveStatus(public_item.id) != ui::geocaching::DraftSaveStatus::Pending; },
          "archive retry stuck");
    require(test::source->archiveStatus(public_item.id) != ui::geocaching::DraftSaveStatus::Failed, "timestamp change broke archive retry");
    if (router.sends > retry_sends)
    {
        reply(router, publicationReply(router, 3), 1);
        until([&]
              { return test::source->archiveStatus(public_item.id) == ui::geocaching::DraftSaveStatus::Saved; },
              "archive retry not confirmed");
    }
    auto archive_overlay = std::make_unique<ui::geocaching::LocalMapOverlay>();
    auto archive_map = std::make_unique<ui::map::MapOverlaySnapshot>();
    until([&]
          {
              archive_overlay->update(*test::source, 31, 121, 15);
              archive_map = std::make_unique<ui::map::MapOverlaySnapshot>();
              archive_overlay->append(*archive_map);
              return archive_overlay->finished(); },
          "archive map did not finish local metadata");
    require(archive_map->item_count == 1, "archived local cache remained on Map or hid the downloaded cache");
    test::source->requestWindow(Section::Downloaded, 0, 4);
    ui::geocaching::Item offline_copy;
    until([&]
          { const auto view = snapshot(Section::Downloaded); return view.ready && test::source->item(Section::Downloaded, 0, view.generation, offline_copy); },
          "offline removal row missing");
    require(test::source->removeDownloaded(offline_copy), "offline removal not queued");
    until([&]
          { return test::source->downloadedRemovalStatus(offline_copy.id, offline_copy.revision_hash) != ui::geocaching::DraftSaveStatus::Pending; },
          "offline removal stuck");
    require(test::source->downloadedRemovalStatus(offline_copy.id, offline_copy.revision_hash) == ui::geocaching::DraftSaveStatus::Saved, "offline removal failed");
    until([&]
          { const auto view = snapshot(Section::Downloaded); return view.ready && !view.count; },
          "deleted offline copy remained in Downloaded");
    until([&]
          { archive_overlay->update(*test::source, 31, 121, 15); archive_map->item_count = 0; archive_overlay->append(*archive_map); return archive_overlay->finished() && !archive_map->item_count; },
          "same-viewport map retained deleted offline marker");
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "session did not close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "session leaked buffers or file handles");
    require(!router.service && !router.announcement && !router.delivery, "session did not release background transport");
    test::files = active_fixture;
    test::directories = active_directories;
    const auto disk = test::files;
    const auto disk_directories = test::directories;
    std::fprintf(stderr, "Runtime: healthy restart\n");
    // Cold offline startup has no dispatch destination or QueryClient yet.
    // Saved rows and full details must not wait for either to become available.
    router.ready = false;
    // Open the main map directly, without warming either Geocaching list.
    // Include a synthetic per-operation storage cost; UI refresh deadlines stay
    // at 750 ms instead of adding a fresh 750 ms after background work finishes.
    test::io_delay_ms = std::getenv("TRAIL_MATE_TEST_IO_DELAY_MS")
                            ? static_cast<uint32_t>(std::strtoul(std::getenv("TRAIL_MATE_TEST_IO_DELAY_MS"), nullptr, 10))
                            : 5;
    {
        test::source->activate(true);
        auto overlays = std::make_unique<ui::geocaching::LocalMapOverlay>();
        auto map = std::make_unique<ui::map::MapOverlaySnapshot>();
        const auto map_started = test::clock_ms;
        const auto map_io_started = test::io_operations;
        const bool previous_profile = test::profile_reads;
        test::profile_reads = true;
        // Diagnostic override only. The default models the production Map
        // timer; faster polling must not silently relax the acceptance test.
        const auto refresh_ms = std::getenv("TRAIL_MATE_TEST_MAP_REFRESH_MS")
                                    ? std::max(1UL, std::strtoul(std::getenv("TRAIL_MATE_TEST_MAP_REFRESH_MS"), nullptr, 10))
                                    : ui::geocaching::LocalMapOverlay::kPollIntervalMs;
        test::read_bytes_by_path.clear();
        for (unsigned frame = 0; frame < 100 && map->item_count != 2; ++frame)
        {
            const auto refresh_due = test::clock_ms + refresh_ms;
            overlays->update(*test::source, 31, 121, 15);
            map = std::make_unique<ui::map::MapOverlaySnapshot>();
            overlays->append(*map);
            if (test::profile_reads)
                std::fprintf(stderr, "Map refresh: elapsed_ms=%llu markers=%u io_operations=%llu\n",
                             static_cast<unsigned long long>(test::clock_ms - map_started), static_cast<unsigned>(map->item_count),
                             static_cast<unsigned long long>(test::io_operations - map_io_started));
            if (map->item_count == 2) break;
            while (test::clock_ms < refresh_due) tick();
        }
        require(map->item_count == 2 && map->header.valid, "direct main map did not load local markers");
        for (const auto& read : test::read_bytes_by_path)
            require(read.first.find("/05/") == std::string::npos && read.first.find("/0a/") == std::string::npos,
                    "main Map audited publication requests or parent tasks");
        test::profile_reads = previous_profile;
        std::fprintf(stderr, "Direct Map cold-start acceptance: elapsed_ms=%llu budget_ms=3000 io_delay_ms=%u\n",
                     static_cast<unsigned long long>(test::clock_ms - map_started), test::io_delay_ms);
        std::fprintf(stderr, "Direct Map I/O operations=%llu\n", static_cast<unsigned long long>(test::io_operations - map_io_started));
        if (test::profile_reads)
            for (const auto& read : test::read_bytes_by_path)
                std::fprintf(stderr, "  %llu bytes %s\n", static_cast<unsigned long long>(read.second), read.first.c_str());
        require(test::clock_ms - map_started <= 3000, "main map exceeded the three-second local marker budget");
        test::source->activate(false);
        tick();
        until([&]
              { return std::strstr(snapshot(Section::Published).status.data(), "Starting Geocaching"); },
              "direct map session did not close");
    }
    const auto installed_gpx = std::find_if(disk.begin(), disk.end(), [](const auto& file)
                                            { return file.first.find("/.state/") == std::string::npos && file.first.size() >= 4 && file.first.substr(file.first.size() - 4) == ".gpx"; });
    require(installed_gpx != disk.end(), "restart test requires an installed GPX");
    // Visiting Discover must not make subsequent offline catalog reads depend
    // on auditing unrelated network history.
    {
        test::source->activate(true);
        tick();
        (void)snapshot(Section::Discover);
        tick();
        const auto started = test::clock_ms;
        const auto payload_allocations = test::payload_allocations;
        test::source->requestWindow(Section::Downloaded, 0, 4);
        until([&]
              { const auto view = snapshot(Section::Downloaded); return view.ready && view.count == 1 &&
                       test::source->item(Section::Downloaded, 0, view.generation, item); },
              "Downloaded did not load after visiting Discover");
        require(test::payload_allocations == payload_allocations, "Downloaded allocated a download response buffer");
        test::source->requestWindow(Section::Published, 0, 4);
        until([&]
              { const auto view = snapshot(Section::Published); return view.ready && view.count == 1 &&
                       test::source->item(Section::Published, 0, view.generation, item) && item.has_coordinates; },
              "local draft did not load after visiting Discover");
        std::fprintf(stderr, "Discover to local acceptance: elapsed_ms=%llu budget_ms=3000 io_delay_ms=%u\n",
                     static_cast<unsigned long long>(test::clock_ms - started), test::io_delay_ms);
        require(test::clock_ms - started <= 3000, "visiting Discover made local catalogs wait for full audit");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty() && !router.service &&
                       std::strstr(snapshot(Section::Published).status.data(), "Starting Geocaching"); },
              "Discover to local session did not close");
    }
    test::fail_read_path = installed_gpx->first;
    test::busy_reads = 12;
    test::busy_read_path = "/trailmate/geocaching/.state/index/root.h0";
    test::source->activate(true);
    tick();
    test::source->requestWindow(Section::Downloaded, 0, 4);
    const auto local_list_started = test::clock_ms;
    until([&]
          {
              const auto view = snapshot(Section::Downloaded);
              return view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item) && item.id == saved_id && item.downloaded; },
          "restart lost downloaded map row");
    require(!test::busy_reads, "startup did not wait through contended volume reads");
    test::busy_read_path.clear();
    require((test::clock_ms - local_list_started) / tick_ms < 850,
            "local startup regressed into duplicate index scans or blocking download recovery");
    {
        auto overlays = std::make_unique<ui::geocaching::LocalMapOverlay>();
        auto map = std::make_unique<ui::map::MapOverlaySnapshot>();
        test::fail_read = true;
        test::busy_reads = 12;
        until([&]
              {
                  overlays->update(*test::source, 31, 121, 15);
                  require(!std::strstr(snapshot(Section::Published).status.data(), "Restoring geocaching tasks"),
                          "one local metadata read failure restarted full storage recovery");
                  map = std::make_unique<ui::map::MapOverlaySnapshot>();
                  overlays->append(*map);
                  return map->item_count == 2; },
              "cold offline map lost local cache markers");
        require(!test::fail_read, "map did not encounter the metadata read failure");
        require(!test::busy_reads, "map did not encounter all contended metadata reads");
        ui::geocaching::Item local_draft;
        const auto local_view = snapshot(Section::Published);
        require(local_view.ready && test::source->item(Section::Published, 0, local_view.generation, local_draft) &&
                    local_draft.has_coordinates && std::strstr(local_draft.detail.data(), "Checking publication status"),
                "map waited for publication history instead of showing committed draft metadata");
        require(!router.service && !router.announcement && !router.delivery,
                "local map started network discovery");
    }
    require(test::fail_read_path == installed_gpx->first, "completed download reopened GPX during startup or listing");
    {
        const auto elapsed = test::clock_ms - local_list_started;
        std::fprintf(stderr, "Local cold-start acceptance: elapsed_ms=%llu budget_ms=3000 io_delay_ms=%u\n",
                     static_cast<unsigned long long>(elapsed), test::io_delay_ms);
        require(elapsed <= 3000, "two offline local markers exceeded the three-second startup budget");
    }
    test::io_delay_ms = 0;
    test::fail_read_path.clear();
    require(test::files == disk, "read-only restart changed persisted files");
    require(!router.service && !router.announcement && !router.delivery, "local list started a network service");
    const auto offline_sends = router.sends;
    test::source->open(item, snapshot(Section::Downloaded).generation);
    until([&]
          { return detailText(item).status == ui::geocaching::DetailStatus::Ready; },
          "saved detail unavailable offline after restart");
    require(detailText(item).description == expected_record.description && detailText(item).hint == expected_record.hint,
            "saved detail lost description or hint");
    require(router.sends == offline_sends && test::files == disk, "offline detail used network or rewrote storage");
    require(!router.service, "local detail started a network service");
    test::source->closeDetail();
    tick();
    test::source->requestWindow(Section::Published, 0, 4);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return view.count == 1 && test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 2; },
          "restart lost publication confirmation");
    require(!router.service, "local publication list started a network service");
    router.ready = true;
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "offline session did not start discovery on demand");
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "restored session did not close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "restored session leaked resources");
    // External USB ownership can arrive during an asynchronous query write.
    // Closing the UI must neither touch the card nor busy-spin during it.
    // Browse refresh is independent of USB ownership and SD workspace allocation.
    test::external_owner = true;
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "USB browse did not open");
    const auto no_sd = test::files;
    const auto before_sends = router.sends;
    announce(router);
    // The received announcement itself now lives in PSRAM. Once copied,
    // processing it must not acquire any SD workspace or further allocations.
    test::memory_available = false;
    until([&]
          { return router.sends == before_sends + 1; },
          "browse allocated SD workspace");
    require(test::files == no_sd && !test::blocked_io, "USB browse accessed SD");
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "USB browse did not close");
    test::memory_available = true;
    test::external_owner = false;
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "browse resources leaked");
    ui::geocaching::Snapshot item_snapshot;
    // Corrupt derived metadata must be rebuilt from authoritative facts. A
    // transient read failure must instead preserve the tree and report I/O.
    {
        auto shard = disk.end();
        for (auto file = disk.begin(); file != disk.end(); ++file)
            if (file->first.find("/index/a/04/") != std::string::npos && file->first.find(".gci.c") != std::string::npos &&
                (shard == disk.end() || file->first > shard->first)) shard = file;
        require(shard != disk.end(), "local recovery test requires a draft shard");
        test::files = disk;
        test::directories = disk_directories;
        test::files.at(shard->first).back() ^= 1;
        const auto sends_before = router.sends;
        test::source->activate(true);
        tick();
        until([&]
              { const auto view = snapshot(Section::Published); return view.ready && view.count == 1 &&
                       test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed; },
              "offline corrupt draft did not fall back to verified index repair");
        require(router.sends == sends_before && !router.service, "offline index repair started networking");
        test::source->activate(false);
        until([&]
              { return test::allocations.empty() && std::strstr(snapshot(Section::Published).status.data(), "Starting Geocaching"); },
              "offline repaired session did not close");
    }
    unsigned damaged_files = 0;
    std::fprintf(stderr, "Runtime: startup fault injection\n");
    for (unsigned fault = 0; fault < 3; ++fault)
        for (const auto& entry : disk)
        {
            if (entry.first.find("/.state/index/") == std::string::npos || entry.second.empty()) continue;
            if (fault == 2 && entry.first.substr(entry.first.size() - 4) != ".gci") continue;
            test::files = disk;
            test::directories = disk_directories;
            if (fault == 0) test::files[entry.first].back() ^= 1;
            else if (fault == 1) test::files[entry.first].pop_back();
            else test::fail_read_path = entry.first;
            const char* expected = fault == 2 ? "Cannot read cached storage" : "";
            const auto damaged = test::files;
            std::fprintf(stderr, "Startup fault %u: %s\n", fault, entry.first.c_str());
            test::source->activate(true);
            tick();
            test::source->snapshot(Section::Discover, item_snapshot);
            test::source->snapshot(Section::Published, item_snapshot);
            until([&]
                  {
                  const auto view = snapshot(Section::Published);
                      return view.can_create || std::strstr(view.status.data(), "Cannot read cached storage") || std::strstr(view.status.data(), "Cached index needs recovery"); },
                  "damaged-index startup did not reach a terminal status");
            if (snapshot(Section::Published).can_create)
            {
                // An actual write request requires full audit. Deliberately
                // stale deletion exercises that boundary without changing data.
                const uint64_t stale_generation = UINT64_MAX - 1;
                require(test::source->deleteDraft(draft.id, stale_generation), "audit probe was not queued");
                until([&]
                      { const auto view = snapshot(Section::Published); return
                               test::source->draftSaveStatus(draft.id, stale_generation) == ui::geocaching::DraftSaveStatus::Failed ||
                               std::strstr(view.status.data(), "Cannot read cached storage") ||
                               std::strstr(view.status.data(), "Cached index needs recovery"); },
                      "write request did not complete index audit");
            }
            require(fault == 2 ? std::strstr(snapshot(Section::Published).status.data(), expected) != nullptr : snapshot(Section::Published).can_create,
                    "index repair did not reach the expected result");
            require(test::fail_read_path.empty(), "startup did not reach the injected read failure");
            if (fault == 2) require(test::files == damaged, "I/O failure modified persistent evidence");
            else
            {
                for (const auto& original : disk)
                    if (original.first.find("/.state/index/") == std::string::npos)
                        require(test::files.at(original.first) == original.second, "index repair changed authoritative data");
                require(!test::directories.count("/trailmate/geocaching/.state/index.repair"), "verified repair retained its temporary archive");
                test::source->requestWindow(Section::Downloaded, 0, 4);
                until([&]
                      { const auto view = snapshot(Section::Downloaded); return view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item) && item.id == saved_id && item.downloaded; },
                      "rebuild lost the downloaded map row");
                test::source->requestWindow(Section::Published, 0, 4);
                until([&]
                      { const auto view = snapshot(Section::Published); return view.count == 1 && test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 2; },
                      "rebuild lost the confirmed publication");
            }
            test::source->activate(false);
            tick();
            until([&]
                  { return !router.service && test::allocations.empty() &&
                           std::strstr(snapshot(Section::Discover).status.data(), "Starting Geocaching"); },
                  "damaged-index session did not close");
            require(test::allocations.empty() && !test::open_files && !test::open_dirs, "damaged-index recovery leaked resources");
            ++damaged_files;
        }
    require(damaged_files > 3, "damaged-index cases did not cover persisted shards");
    test::files = disk;
    test::directories = disk_directories;
    test::source->activate(true);
    tick();
    test::source->snapshot(Section::Published, item_snapshot);
    until([&]
          { return snapshot(Section::Published).can_create; },
          "healthy storage did not recover after failed startups");
    require(test::files == disk, "healthy recovery after faults changed persistent data");
    test::source->activate(false);
    until([&]
          { return !router.service && std::strstr(snapshot(Section::Published).status.data(), "Starting Geocaching"); },
          "healthy session after faults did not close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "healthy recovery after faults leaked resources");
    // Capture actual disk states from the device startup, then recreate the
    // Session from each interrupted state. Closing the old owner is only host
    // cleanup; its post-close files are replaced by the captured crash image.
    struct CrashImage
    {
        decltype(test::files) files;
        decltype(test::directories) directories;
        bool captured = false;
    };
    std::array<CrashImage, 8> cuts;
    const std::string index = "/trailmate/geocaching/.state/index/";
    const std::string archive = "/trailmate/geocaching/.state/index.repair/";
    auto rootSequence = [&](const std::string& path)
    {
        const auto found = test::files.find(path);
        uint64_t sequence = 0;
        if (found != test::files.end() && found->second.size() == 468)
            for (unsigned i = 28; i < 36; ++i) sequence = (sequence << 8) | found->second[i];
        return sequence;
    };
    auto closeRuntime = [&]
    {
        test::source->activate(false);
        tick();
        until([&]
              { return !router.service && test::allocations.empty() &&
                       std::strstr(snapshot(Section::Discover).status.data(), "Starting Geocaching"); },
              "interrupted session did not release resources");
        require(!test::open_files && !test::open_dirs, "interrupted session leaked file handles");
    };
    test::files = disk;
    test::directories = disk_directories;
    const auto committed = std::max(rootSequence(index + "root.h0"), rootSequence(index + "root.h1"));
    size_t original_index_files = 0;
    for (const auto& file : disk)
        if (!file.first.compare(0, index.size(), index)) ++original_index_files;
    test::files[index + "root.h0"].back() ^= 1;
    test::source->activate(true);
    tick();
    test::source->snapshot(Section::Published, item_snapshot);
    until([&]
          {
              const bool archived = test::directories.count("/trailmate/geocaching/.state/index.repair");
              const bool primary = test::directories.count("/trailmate/geocaching/.state/index");
              size_t archive_files = 0;
              bool marker = false, empty_shard = false, unheaded_shard = false;
              for (const auto& file : test::files)
              {
                  if (!file.first.compare(0, archive.size(), archive)) ++archive_files;
                  marker |= !file.first.compare(0, archive.size() + 5, archive + "gcf1-");
                  if (!file.first.compare(0, index.size(), index) && file.first.find(".gci.h0") != std::string::npos)
                      unheaded_shard |= !test::files.count(file.first.substr(0, file.first.size() - 1) + '1');
                  if (file.first.compare(0, index.size(), index) || file.first.substr(file.first.size() - 4) != ".gci") continue;
                  empty_shard |= file.second.empty();
              }
              const auto first = test::files.find(index + "root.h0");
              const auto sequence = std::max(rootSequence(index + "root.h0"), rootSequence(index + "root.h1"));
              const bool points[] = {archived && !primary && !marker, archived && marker && !primary,
                                     primary && first != test::files.end() && first->second.empty(),
                                     primary && first != test::files.end() && first->second.size() == 468 && !test::files.count(index + "root.h1"),
                                     archived && empty_shard, archived && unheaded_shard,
                                     archived && sequence > 0 && sequence < committed,
                                     archived && sequence >= committed && archive_files < original_index_files};
              for (size_t i = 0; i < cuts.size(); ++i)
                  if (points[i] && !cuts[i].captured) cuts[i] = {test::files, test::directories, true};
              return snapshot(Section::Published).can_create; },
          "repair baseline for interruption testing failed");
    closeRuntime();
    for (size_t cut = 0; cut < cuts.size(); ++cut)
    {
        require(cuts[cut].captured, "repair interruption point was not observed");
        std::fprintf(stderr, "Runtime: resume repair cut %u\n", static_cast<unsigned>(cut));
        test::files = cuts[cut].files;
        test::directories = cuts[cut].directories;
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "interrupted repair did not resume");
        require(!test::directories.count("/trailmate/geocaching/.state/index.repair"), "resumed repair retained archive");
        for (const auto& original : disk)
            if (original.first.compare(0, index.size(), index))
                require(test::files.at(original.first) == original.second, "resumed repair changed authoritative data");
        closeRuntime();
    }
    // Remove the last complete journal transaction. Rebuilding may produce a
    // valid older root, but must not admit it below the durable repair floor.
    test::files = disk;
    test::directories = disk_directories;
    std::string journal;
    for (const auto& file : disk)
        if (file.first.find("/.state/journal/") != std::string::npos) journal = file.first;
    require(!journal.empty(), "journal fixture missing");
    auto& journal_bytes = test::files[journal];
    size_t offset = 0, last_frame = 0;
    while (offset + 24 <= journal_bytes.size())
    {
        uint32_t payload = 0;
        for (unsigned i = 8; i < 12; ++i) payload = (payload << 8) | journal_bytes[offset + i];
        last_frame = offset;
        offset += 24 + payload;
    }
    require(offset == journal_bytes.size() && offset, "journal framing did not match the fixture");
    journal_bytes.resize(last_frame);
    const auto incomplete_journal = journal_bytes;
    if (!last_frame) test::files.erase(journal);
    // Keep both committed root witnesses intact while damaging one shard.
    for (auto& file : test::files)
        if (!file.first.compare(0, index.size(), index) && file.first.substr(file.first.size() - 4) == ".gci")
        {
            file.second.back() ^= 1;
            break;
        }
    for (unsigned restart = 0; restart < 2; ++restart)
    {
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return std::strstr(snapshot(Section::Published).status.data(), "Cached index needs recovery"); },
              "missing committed journal tail was accepted");
        require(last_frame ? test::files.at(journal) == incomplete_journal : !test::files.count(journal), "failed repair modified incomplete journal");
        require(test::directories.count("/trailmate/geocaching/.state/index.repair"), "failed repair lost its archive");
        closeRuntime();
        // The durable floor must survive even if the old root copies are lost.
        test::files.erase(archive + "root.h0");
        test::files.erase(archive + "root.h1");
    }
    test::files[journal] = disk.at(journal);
    test::source->activate(true);
    tick();
    test::source->snapshot(Section::Published, item_snapshot);
    until([&]
          { return snapshot(Section::Published).can_create; },
          "restored authoritative journal did not finish repair");
    closeRuntime();
    std::puts("Interrupted repair resumed at 8 disk states; missing committed journal tail rejected across restarts");
    std::printf("Startup handled %u index corruption/truncation/read-failure cases\n", damaged_files);
    require(!test::ui_io, "interface callback touched storage");
    // Populate genuine journal/index history, then let the production session
    // trigger maintenance through its normal public storage owner scheduling.
    {
        namespace sd = platform::esp::arduino_common::geocaching;
        namespace gc = geocaching::storage;
        test::in_ui = false;
        gc::VolumeInstance volume;
        require(sd::inspectSdVolume(volume) == sd::SdVolumeResult::Ready, "maintenance volume missing");
        std::array<gc::IndexRootBytes, 2> roots;
        gc::IndexRootView first, second, root;
        for (unsigned i = 0; i < 2; ++i)
        {
            const auto& bytes = test::files.at(index + "root.h" + std::to_string(i));
            require(bytes.size() == roots[i].size(), "maintenance root size mismatch");
            std::copy(bytes.begin(), bytes.end(), roots[i].begin());
        }
        require(gc::decodeIndexRoot({roots[0].data(), roots[0].size()}, volume, first) &&
                    gc::decodeIndexRoot({roots[1].data(), roots[1].size()}, volume, second) && gc::selectIndexRoot(first, second, root),
                "maintenance root selection failed");
        unsigned copy = second.revision > first.revision ? 1 : 0;
        std::array<uint8_t, 8192> frame;
        std::array<uint8_t, 32> key{};
        key.fill(0xcc);
        const uint8_t value[] = {0x92, 0x90, 0x90};
        const gc::MutationView row{11, {key.data(), key.size()}, {value, sizeof(value)}, false};
        while (root.sequence < 260)
        {
            sd::SdIndexedCommit commit(volume);
            require(commit.begin(root, copy, &row, 1, frame.data(), frame.size(), roots[1 - copy]), "maintenance fixture commit rejected");
            auto status = sd::IndexedCommitStep::Working;
            for (unsigned i = 0; i < 100000 && status == sd::IndexedCommitStep::Working; ++i) status = commit.step();
            require(status == sd::IndexedCommitStep::Verified && commit.committed(root), "maintenance fixture commit failed");
            copy = 1 - copy;
        }
        const char old_slot = root.slot;
        const auto requireWritableStorage = [&]
        {
            // Read-only directory browsing must not start checkpoint work.
            // A stale write admits maintenance without changing saved content.
            const uint64_t stale_generation = UINT64_MAX - 1;
            require(test::source->deleteDraft(draft.id, stale_generation), "maintenance audit request rejected");
            until([&]
                  { return test::source->draftSaveStatus(draft.id, stale_generation) == ui::geocaching::DraftSaveStatus::Failed; },
                  "maintenance audit request did not finish");
        };
        const auto initial_sends = router.sends;
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "maintenance fixture did not open");
        requireWritableStorage();
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "maintenance network browser did not start on demand");
        announce(router);
        until([&]
              { return router.sends == initial_sends + 1; },
              "maintenance capabilities request missing");
        reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
        until([&]
              { return router.sends == initial_sends + 2; },
              "maintenance query request missing");
        reply(router, fixture(folder, "query-response-v1.bin"), 2);
        until([&]
              { return snapshot(Section::Discover).can_refresh; },
              "maintenance query did not finish");
        until([&]
              { return test::files.count("/trailmate/geocaching/.state/checkpoint/a.gcs") &&
                       !test::directories.count(index + old_slot) && !test::allocated("geocaching.index.read"); },
              "production session did not rotate checkpoint and release workspace");
        require(test::files.at(index + "root.h0") == test::files.at(index + "root.h1"), "session maintenance left different roots");
        require(snapshot(Section::Discover).count == 1, "maintenance lost current query page");
        closeRuntime();
        // A recently completed checkpoint is no longer eligible on restart.
        // Add real committed history before testing foreground interruption.
        appendMaintenanceHistory(256);
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "session checkpoint failed restart recovery");
        requireWritableStorage();
        const auto resumed_sends = router.sends;
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "resumed network browser did not start on demand");
        announce(router);
        until([&]
              { return router.sends == resumed_sends + 1; },
              "yield test capabilities missing");
        reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
        until([&]
              { return router.sends == resumed_sends + 2; },
              "yield test query missing");
        reply(router, fixture(folder, "query-response-v1.bin"), 2);
        until([&]
              { return snapshot(Section::Discover).can_refresh && test::files.count("/trailmate/geocaching/.state/staging/checkpoint.gcs"); },
              "yield test did not enter checkpoint build");
        ui::geocaching::DraftInput foreground;
        foreground.name = "Saved while maintenance was running";
        require(test::source->saveDraft(foreground), "maintenance rejected foreground draft");
        const auto queued_at = test::clock_ms;
        until([&]
              { return test::source->draftSaveStatus(foreground.id, 0) == ui::geocaching::DraftSaveStatus::Saved; },
              "maintenance did not yield to draft save");
        require(test::clock_ms - queued_at < 10000, "foreground waited for the full checkpoint rotation");
        require(!test::files.count("/trailmate/geocaching/.state/checkpoint/b.gcs"), "foreground unnecessarily waited for checkpoint publication");
        closeRuntime();
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "pagination restart failed");
        requireWritableStorage();
        const auto paging_sends = router.sends;
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "pagination network browser did not start on demand");
        announce(router);
        until([&]
              { return router.sends == paging_sends + 1; },
              "pagination capabilities missing");
        reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
        until([&]
              { return router.sends == paging_sends + 2; },
              "pagination query missing");
        reply(router, paginationFixture(folder, false), 2);
        // The previous interrupted builder left a staging file. Wait for the
        // next builder to remove and recreate it, proving maintenance is
        // running now instead of mistaking that old file for new progress.
        bool removed_staging = false;
        until([&]
              {
            const auto found = test::files.find("/trailmate/geocaching/.state/staging/checkpoint.gcs");
            if (found == test::files.end()) removed_staging = true;
            return removed_staging && snapshot(Section::Discover).has_more && found != test::files.end() && found->second.empty(); },
              "pagination test did not enter maintenance");
        require(test::source->loadMore(), "maintenance rejected pagination intent");
        require(!test::source->loadMore() && !snapshot(Section::Discover).has_more, "duplicate pagination was accepted");
        require(!test::ui_io, "pagination callback touched storage");
        until([&]
              { return router.sends == paging_sends + 3; },
              "queued pagination was not dispatched");
        require(!test::files.count("/trailmate/geocaching/.state/checkpoint/b.gcs"), "pagination waited for checkpoint publication");
        reply(router, paginationFixture(folder, true), 2);
        until([&]
              { const auto view = snapshot(Section::Discover); return view.can_refresh && !view.has_more && view.count == 0; },
              "final pagination response was not displayed");
        require(router.sends == paging_sends + 3, "pagination request sent twice");
        closeRuntime();
        const auto journalCount = []
        {
            size_t count = 0;
            for (const auto& file : test::files)
                if (file.first.find("/journal/") != std::string::npos) ++count;
            return count;
        };
        const auto journals_before = journalCount();
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "reclamation session did not recover");
        requireWritableStorage();
        const auto reclaim_sends = router.sends;
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "reclamation network browser did not start on demand");
        announce(router);
        until([&]
              { return router.sends == reclaim_sends + 1; },
              "reclamation capabilities missing");
        reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
        until([&]
              { return router.sends == reclaim_sends + 2; },
              "reclamation query missing");
        reply(router, fixture(folder, "query-response-v1.bin"), 2);
        until([&]
              { return test::files.count("/trailmate/geocaching/.state/checkpoint/b.gcs") &&
                       journalCount() < journals_before && !test::allocated("geocaching.index.read") &&
                       test::files.at(index + "root.h0") == test::files.at(index + "root.h1"); },
              "production session did not reclaim covered journals");
        std::fprintf(stderr, "Runtime reclaimed journals: %u -> %u\n", static_cast<unsigned>(journals_before), static_cast<unsigned>(journalCount()));
        closeRuntime();
        // Force startup to use the older checkpoint and retained log suffix.
        // Verify business state through the same UI source the device uses.
        test::files.at("/trailmate/geocaching/.state/checkpoint/b.gcs").back() ^= 1;
        test::source->activate(true);
        tick();
        test::source->snapshot(Section::Published, item_snapshot);
        until([&]
              { return snapshot(Section::Published).can_create; },
              "reclaimed journal fallback could not recover session");
        test::source->requestWindow(Section::Downloaded, 0, 4);
        until([&]
              { const auto view = snapshot(Section::Downloaded); return view.count == 1 &&
                       test::source->item(Section::Downloaded, 0, view.generation, item) && item.id == saved_id && item.downloaded; },
              "reclaimed journal fallback lost downloaded cache");
        test::source->requestWindow(Section::Published, 0, 4);
        until([&]
              {
                  const auto view = snapshot(Section::Published);
                  if (view.count != 2) return false;
                  bool confirmed = false, foreground_saved = false;
                  for (size_t i = 0; i < view.count; ++i)
                  {
                      if (!test::source->item(Section::Published, i, view.generation, item)) return false;
                      if (std::equal(draft.id.begin(), draft.id.end(), item.id.begin()))
                          confirmed = item.publication_confirmed && item.publication_revision == 2;
                      if (std::equal(foreground.id.begin(), foreground.id.end(), item.id.begin())) foreground_saved = item.is_draft;
                  }
                  return confirmed && foreground_saved; },
              "reclaimed journal fallback lost publication or foreground draft");
        size_t preserved_gpx = 0;
        for (const auto& file : disk)
            if (file.first.size() >= 4 && file.first.substr(file.first.size() - 4) == ".gpx")
            {
                require(test::files.at(file.first) == file.second, "reclamation modified installed GPX");
                ++preserved_gpx;
            }
        require(preserved_gpx > 0, "reclamation test had no installed GPX to verify");
        closeRuntime();
    }
    // Deleting a local draft is an offline transaction and survives restart.
    test::source->activate(true);
    tick();
    until([&]
          { return snapshot(Section::Published).can_create; },
          "delete session did not open");
    test::source->requestWindow(Section::Published, 0, 4);
    ui::geocaching::Item deleting;
    until([&]
          { const auto view = snapshot(Section::Published); return view.ready && test::source->item(Section::Published, 0, view.generation, deleting); },
          "delete row missing");
    std::array<uint8_t, 16> delete_id;
    std::copy_n(deleting.id.data(), delete_id.size(), delete_id.data());
    const auto count_before_delete = snapshot(Section::Published).count;
    const auto delete_sends = router.sends;
    require(test::source->deleteDraft(delete_id, deleting.edit_generation + 1), "stale delete was not queued");
    until([&]
          { return test::source->draftSaveStatus(delete_id, deleting.edit_generation + 1) != ui::geocaching::DraftSaveStatus::Pending; },
          "stale delete did not finish");
    require(test::source->draftSaveStatus(delete_id, deleting.edit_generation + 1) == ui::geocaching::DraftSaveStatus::Failed, "stale version deleted a cache");
    require(snapshot(Section::Published).count == count_before_delete, "failed delete changed the catalog");
    require(test::source->deleteDraft(delete_id, deleting.edit_generation), "delete was not queued");
    until([&]
          { return test::source->draftSaveStatus(delete_id, deleting.edit_generation) != ui::geocaching::DraftSaveStatus::Pending; },
          "delete did not finish");
    require(test::source->draftSaveStatus(delete_id, deleting.edit_generation) == ui::geocaching::DraftSaveStatus::Saved, "delete failed");
    until([&]
          { const auto view = snapshot(Section::Published); return view.ready && view.count == count_before_delete - 1; },
          "deleted row stayed in the open list");
    closeRuntime();
    test::source->activate(true);
    tick();
    until([&]
          { const auto view = snapshot(Section::Published); return view.ready && view.count == count_before_delete - 1; },
          "deleted row returned after restart");
    require(router.sends == delete_sends, "local deletion started network traffic");
    closeRuntime();
    // A missing directory response has a bounded lifetime without any SD work.
    const auto disk_before_timeout = test::files;
    test::card_ready = false;
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "timeout session did not open");
    const auto sends_before_timeout = router.sends;
    announce(router);
    until([&]
          { return router.sends > sends_before_timeout; },
          "timeout query missing");
    const auto retry_request = router.sent_bytes;
    tick_ms = 1000;
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Directory did not reply"); },
          "query did not time out");
    require(router.sends > sends_before_timeout + 1 && router.sends <= sends_before_timeout + 9 && router.sent_bytes == retry_request,
            "query retries were unbounded or changed the request");
    const auto stopped_sends = router.sends;
    for (unsigned i = 0; i < 20; ++i) tick();
    require(router.sends == stopped_sends && test::files == disk_before_timeout && !test::blocked_io,
            "timed out browsing retried or accessed SD");
    tick_ms = 5;
    closeRuntime();
    std::puts("Production runtime discovery/download/publication/GPX/restart/ownership/allocation/checkpoint rotation passed");
}
