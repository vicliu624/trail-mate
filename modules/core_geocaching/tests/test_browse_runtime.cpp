#include "geocaching/protocol/publish_request.h"
#include "geocaching/protocol/query_response.h"
#include "platform/esp/arduino_common/geocaching/browse_runtime.h"
#include "platform/esp/arduino_common/geocaching/sd_indexed_commit.h"
#include "runtime_environment.h"
#include "storage_owner.h"
#include <cstdio>
#include <fstream>
#include <iterator>

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
void until(Predicate ready, const char* message)
{
    test::profile_reads = std::getenv("TRAIL_MATE_TEST_IO_PROFILE") != nullptr;
    test::read_bytes_by_path.clear();
    const auto started = test::clock_ms;
    const auto begins = test::maintenance::begins;
    unsigned ticks = 0;
    for (; ticks < 100000 && !ready(); ++ticks) tick();
    std::fprintf(stderr, "Runtime wait: %s; ticks=%u simulated_ms=%llu owner_begins=%llu\n", message, ticks,
                 static_cast<unsigned long long>(test::clock_ms - started),
                 static_cast<unsigned long long>(test::maintenance::begins - begins));
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
        for (size_t i = 0; i < std::min<size_t>(5, reads.size()); ++i)
            std::fprintf(stderr, "  %llu bytes %s\n", static_cast<unsigned long long>(reads[i].first), reads[i].second.c_str());
    }
    if (!ready())
    {
        ui::geocaching::Snapshot view;
        test::source->snapshot(Section::Discover, view);
        std::fprintf(stderr, "Runtime terminal status: %s\n", view.status.data());
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
int main(int argc, char** argv)
{
    require(argc == 2, "expected fixture directory");
    const std::string folder(argv[1]);
    std::fprintf(stderr, "Runtime: discovery/download/publication\n");
    chat::MeshAdapterRouter router;
    LoraBoard board;
    rt::configure(router, board);
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
    // Simulate a power loss with a durable query still awaiting its response.
    // Close only to release this process's resources, then restore crash media.
    const auto interrupted_files = test::files;
    const auto interrupted_directories = test::directories;
    const auto obsolete_request = router.sent_bytes;
    reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "interrupted query did not close");
    require(!test::allocated("geocaching.rx"), "closing session leaked its pending network reply");
    test::files = interrupted_files;
    test::directories = interrupted_directories;
    test::clock_ms += 180000; // the retained attempt is eligible for retry after reboot
    router.sends = 0;
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Restoring previous queries"); },
          "previous query retirement did not start");
    until([&]
          { return test::files != interrupted_files; },
          "previous query retirement did not write");
    // A second power loss during cancellation must recover its journal before
    // starting a fresh query, without losing the original task history.
    const auto retiring_files = test::files;
    const auto retiring_directories = test::directories;
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "interrupted retirement did not close");
    test::files = retiring_files;
    test::directories = retiring_directories;
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "interrupted query did not recover");
    announce(router);
    until([&]
          { return router.sends == 1; },
          "fresh query was blocked by an obsolete request");
    require(router.sent_bytes != obsolete_request, "restart retransmitted the old session's query");
    // Real SD operations in the remote capture take tens of milliseconds.
    // Exercise the receive handoff and response scheduling at that cadence.
    tick_ms = 40;
    reply(router, fixture(folder, "capabilities-response-v1.bin"), 0);
    until([&]
          { return router.sends == 2; },
          "query request not dispatched");
    require(test::maintenance::begins * 2 < test::maintenance::slices,
            "ongoing Geocaching work restarted the shared owner for every small slice");
    reply(router, fixture(folder, "query-response-v1.bin"), 2);
    until([&]
          { const auto view = snapshot(Section::Discover); return view.count == 1 && view.can_refresh; },
          "query page not persisted");
    tick_ms = 5;
    std::array<uint8_t, 16> reply_remote{};
    require(router.delivery({{reply_remote.data(), 16}, {router.local.data(), 16}, {}, {last_reply.data(), last_reply.size()}}, router.context),
            "durably committed reply was not acknowledged on retry");
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
    require(test::source->download(item, generation), "download not queued");
    until([&]
          { return router.sends == 3; },
          "download request not dispatched");
    reply(router, fixture(folder, "get-response-v1.bin"), 3);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Shared caches"); },
          "download did not finish");
    test::source->requestWindow(Section::Downloaded, 0, 4);
    until([&]
          {
              const auto view = snapshot(Section::Downloaded);
              return view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item) && item.downloaded; },
          "installed GPX missing from directory");
    const auto saved_id = item.id;
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
    until([&]
          {
              const auto view = snapshot(Section::Published);
              if (!view.count || !test::source->item(Section::Published, 0, view.generation, item)) return false;
              draft_generation = item.edit_generation;
              return item.is_draft; },
          "saved draft missing from publication list");
    std::array<uint8_t, 64> author;
    uint32_t from = 0, to = 0;
    until([&]
          { return test::source->publicationAuthor(draft.id, draft_generation, author, &from, &to); },
          "draft not ready for publication confirmation");
    require(from == 0 && to == 1 && test::source->publishDraft(draft.id, draft_generation, author, to), "publication confirmation rejected");
    until([&]
          { return router.sends == 4; },
          "publication request not dispatched");
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
          { return router.sends == 5; },
          "second publication request not dispatched");
    reply(router, publicationReply(router, 2), 1);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 2; },
          "second publication receipt not reflected in list");
    require(router.identity.signs == 2, "version update signed more than once");
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "session did not close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "session leaked buffers or file handles");
    require(!router.service && !router.announcement && !router.delivery, "session did not release background transport");
    const auto disk = test::files;
    const auto disk_directories = test::directories;
    std::fprintf(stderr, "Runtime: healthy restart\n");
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "restart did not restore indexed state");
    test::source->requestWindow(Section::Downloaded, 0, 4);
    until([&]
          {
              const auto view = snapshot(Section::Downloaded);
              return view.count == 1 && test::source->item(Section::Downloaded, 0, view.generation, item) && item.id == saved_id && item.downloaded; },
          "restart lost downloaded map row");
    require(test::files == disk, "read-only restart changed persisted files");
    test::source->requestWindow(Section::Published, 0, 4);
    until([&]
          {
              const auto view = snapshot(Section::Published);
              return view.count == 1 && test::source->item(Section::Published, 0, view.generation, item) && item.publication_confirmed && item.publication_revision == 2; },
          "restart lost publication confirmation");
    test::source->activate(false);
    until([&]
          { return !router.service; },
          "restored session did not close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "restored session leaked resources");
    // External USB ownership can arrive during an asynchronous query write.
    // Closing the UI must neither touch the card nor busy-spin during it.
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "ownership case did not open");
    announce(router);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Saving query progress") && test::allocated("geocaching.index.read"); },
          "query never acquired storage workspace");
    const auto before_external = test::files;
    test::external_owner = true;
    test::source->activate(false);
    tick();
    require(!test::blocked_io && test::files == before_external, "closing query accessed externally owned SD");
    for (unsigned i = 0; i < 20; ++i) tick();
    require(!test::blocked_io, "waiting close touched externally owned SD");
    require(!rt::workPending(), "closed UI ignored SD retry backoff");
    test::external_owner = false;
    until([&]
          { return !router.service; },
          "query close did not resume after SD ownership returned");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "ownership case leaked resources");

    // A temporary allocation failure is retryable and must not write anything.
    test::source->activate(true);
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "allocation case did not open");
    test::memory_available = false;
    announce(router);
    const auto before_memory_wait = test::files;
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Waiting for storage workspace"); },
          "allocation failure was not deferred");
    require(test::files == before_memory_wait && !test::allocated("geocaching.index.read"), "allocation failure retained buffers or changed storage");
    test::source->activate(false);
    for (unsigned i = 0; i < 20 && rt::workPending(); ++i) tick();
    require(!rt::workPending(), "closed UI ignored allocation retry backoff");
    test::memory_available = true;
    until([&]
          { return !router.service; },
          "allocation recovery did not finish close");
    require(test::allocations.empty() && !test::open_files && !test::open_dirs, "allocation case leaked resources");
    // Corrupt derived metadata must be rebuilt from authoritative facts. A
    // transient read failure must instead preserve the tree and report I/O.
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
            const char* expected = fault == 2 ? "Cannot read cached storage" : "Finding a public directory";
            const auto damaged = test::files;
            std::fprintf(stderr, "Startup fault %u: %s\n", fault, entry.first.c_str());
            test::source->activate(true);
            until([&]
                  {
                  const auto view = snapshot(Section::Discover);
                      return std::strstr(view.status.data(), expected) || std::strstr(view.status.data(), "Cached index needs recovery") ||
                         std::strstr(view.status.data(), "Finding a public directory"); },
                  "damaged-index startup did not reach a terminal status");
            require(std::strstr(snapshot(Section::Discover).status.data(), expected),
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
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "healthy storage did not recover after failed startups");
    require(test::files == disk, "healthy recovery after faults changed persistent data");
    test::source->activate(false);
    until([&]
          { return !router.service; },
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
              return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
          "repair baseline for interruption testing failed");
    closeRuntime();
    for (size_t cut = 0; cut < cuts.size(); ++cut)
    {
        require(cuts[cut].captured, "repair interruption point was not observed");
        std::fprintf(stderr, "Runtime: resume repair cut %u\n", static_cast<unsigned>(cut));
        test::files = cuts[cut].files;
        test::directories = cuts[cut].directories;
        test::source->activate(true);
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
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
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Cached index needs recovery"); },
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
    until([&]
          { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
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
        const auto initial_sends = router.sends;
        test::source->activate(true);
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "maintenance fixture did not open");
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
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "session checkpoint failed restart recovery");
        const auto resumed_sends = router.sends;
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
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "pagination restart failed");
        const auto paging_sends = router.sends;
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
              "final pagination response was not persisted");
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
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
              "reclamation session did not recover");
        const auto reclaim_sends = router.sends;
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
        until([&]
              { return std::strstr(snapshot(Section::Discover).status.data(), "Finding a public directory"); },
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
    std::puts("Production runtime discovery/download/publication/GPX/restart/ownership/allocation/checkpoint rotation passed");
}
